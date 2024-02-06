#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <sys/epoll.h>
#include <signal.h>
#include <iostream>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <thread>
#include <chrono>
#include <math.h>
#include <mutex>
#include "base64.h"
#include "websocket.h"

using namespace std;

#define MAX 500000
#define WS_FIN 128
#define bfz 4096

auto start = chrono::high_resolution_clock::now();

char* webSocket::base64_encode(const unsigned char *input, int length)
{
    const auto pl = 4 * ((length + 2) / 3);
    auto output = reinterpret_cast<char *>(calloc(pl + 1, 1)); //+1 for the terminating null that EVP_EncodeBlock adds on
    const auto ol = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output), input, length);
    if (pl != ol)
    {
        cerr << "Whoops, encode predicted " << pl << " but we got " << ol << "\n";
    }
    return output;
}

vector<int> webSocket::getClientIDs(){
    vector<int> clientIDs;
    int clientArrSize = wsClients.size();
    for (int i = 0; i < clientArrSize; i++){
        if (wsClients[i] != NULL)
            clientIDs.push_back(i);
    }

    return clientIDs;
}

bool webSocket::wsSendClientMessage(int clientID, unsigned char opcode, string message) {
    int messageLength = message.size();
    int bufferSize = 4096;
    int frameCount = ceil(static_cast<float>(messageLength) / bufferSize);
    if (frameCount == 0)
        frameCount = 1;

    if (wsClients[clientID]->ReadyState == WS_READY_STATE_CLOSING || wsClients[clientID]->ReadyState == WS_READY_STATE_CLOSED)
        return true;

    for (int i = 0; i < frameCount; i++) {
        unsigned char fin = i != (frameCount - 1) ? 0 : WS_FIN;
        opcode = i != 0 ? WS_OPCODE_CONTINUATION : opcode;

        size_t bufferLength = i != (frameCount - 1) ? bufferSize : (messageLength % bufferSize != 0 ? messageLength % bufferSize : bufferSize);
        size_t totalLength = bufferLength + 2 + (bufferLength > 125 ? (bufferLength > 65535 ? 8 : 2) : 0);

        char *buf = new char[totalLength];
        buf[0] = fin | opcode;

        if (bufferLength <= 125) {
            buf[1] = bufferLength;
            memcpy(buf + 2, message.c_str() + i * bufferSize, bufferLength);
        } else if (bufferLength <= 65535) {
            buf[1] = WS_PAYLOAD_LENGTH_16;
            buf[2] = bufferLength >> 8;
            buf[3] = bufferLength;
            memcpy(buf + 4, message.c_str() + i * bufferSize, bufferLength);
        } else {
            buf[1] = WS_PAYLOAD_LENGTH_63;
            buf[2] = 0;
            buf[3] = 0;
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = bufferLength >> 24;
            buf[7] = bufferLength >> 16;
            buf[8] = bufferLength >> 8;
            buf[9] = bufferLength;
            memcpy(buf + 10, message.c_str() + i * bufferSize, bufferLength);
        }

        int left = totalLength;
        char *buf2 = buf;

        do {
            int sent = send(wsClients[clientID]->socket, buf2, left, 0);
            if (sent == -1) {
                delete[] buf;
                return false;
            }

            left -= sent;
            if (sent > 0)
                buf2 += sent;
        } while (left > 0);

        delete[] buf;
    }


    return true;
}

bool webSocket::wsSend(int clientID, string message, bool binary){
    cout<<"wsSend"<<endl;
    for (int i = 0; i < wsClients.size(); i++){
        if (wsClients[i] == NULL)    continue;
        bool result = wsSendClientMessage(i, binary ? WS_OPCODE_BINARY : WS_OPCODE_TEXT, message);
        if(result == false) return false;
    }
    return true;
}

void webSocket::wsSendClientClose(int clientID, unsigned short status){
    // check if client ready state is already closing or closed
    if (wsClients[clientID]->ReadyState == WS_READY_STATE_CLOSING || wsClients[clientID]->ReadyState == WS_READY_STATE_CLOSED)
        return;

    // store close status
    wsClients[clientID]->ReadyState = status;

    // send close frame to client
    // wsSendClientMessage(clientID, WS_OPCODE_CLOSE, "");
    unsigned char fin = WS_FIN;
    size_t bufferLength = 0;
    size_t totalLength = bufferLength + 2 + (bufferLength > 125 ? (bufferLength > 65535 ? 8 : 2) : 0);
    char *buff = new char[totalLength];
    buff[0] = fin | WS_OPCODE_CLOSE;
    send(wsClients[clientID]->socket, buff, totalLength, 0);
    delete[] buff;

    // set client ready state to closing
    wsClients[clientID]->ReadyState = WS_READY_STATE_CLOSING;
}

void webSocket::wsClose(int clientID){
    wsSendClientClose(clientID, WS_STATUS_NORMAL_CLOSE);
}

bool webSocket::wsCheckSizeClientFrame(int clientID){
    wsClient *client = wsClients[clientID];
    // check if at least 2 bytes have been stored in the frame buffer
    if (client->FrameBytesRead > 1) {
        // fetch payload length in byte 2, max will be 127
        size_t payloadLength = (unsigned char)client->FrameBuffer.at(1) & 127;

        if (payloadLength <= 125){
            // actual payload length is <= 125
            client->FramePayloadDataLength = payloadLength;
        }
        else if (payloadLength == 126){
            // actual payload length is <= 65,535
            if (client->FrameBuffer.size() >= 4){
                vector<unsigned char> length_bytes;
                length_bytes.resize(2);
                memcpy((char*)&length_bytes[0], client->FrameBuffer.substr(2, 2).c_str(), 2);

                size_t length = 0;
                int num_bytes = 2;
                for (int c = 0; c < num_bytes; c++)
                    length += length_bytes[c] << (8 * (num_bytes - 1 - c));
                client->FramePayloadDataLength = length;
            }
        }
        else {
            if (client->FrameBuffer.size() >= 10){
                vector<unsigned char> length_bytes;
                length_bytes.resize(8);
                memcpy((char*)&length_bytes[0], client->FrameBuffer.substr(2, 8).c_str(), 8);

                size_t length = 0;
                int num_bytes = 8;
                for (int c = 0; c < num_bytes; c++)
                    length += length_bytes[c] << (8 * (num_bytes - 1 - c));
                client->FramePayloadDataLength = length;
            }
        }

        return true;
    }

    return false;
}
void webSocket::wsRemoveClient(int clientID){
    if (callOnClose != NULL)
        callOnClose(clientID);

    wsClient *client = wsClients[clientID];

    // fetch close status (which could be false), and call wsOnClose
    // int closeStatus = wsClients[clientID]->CloseStatus;

    // close socket
    close(client->socket);

    socketIDmap.erase(wsClients[clientID]->socket);
    wsClients[clientID] = NULL;
    delete client;
}

bool webSocket::wsProcessClientMessage(int clientID, unsigned char opcode, string data, int dataLength){
    wsClient *client = wsClients[clientID];
    // check opcodes
    if (opcode == WS_OPCODE_PING){
        // received ping message
        return wsSendClientMessage(clientID, WS_OPCODE_PONG, data);
    }
    else if (opcode == WS_OPCODE_PONG){
        // received pong message (it's valid if the server did not send a ping request for this pong message)
        if (client->PingSentTime != 0) {
            client->PingSentTime = 0;
        }
    }
    else if (opcode == WS_OPCODE_CLOSE){
        // received close message
        if (client->ReadyState == WS_READY_STATE_CLOSING){
            // the server already sent a close frame to the client, this is the client's close frame reply
            // (no need to send another close frame to the client)
            client->ReadyState = WS_READY_STATE_CLOSED;
        }
        else {
            // the server has not already sent a close frame to the client, send one now
            wsSendClientClose(clientID, WS_STATUS_NORMAL_CLOSE);
        }

        wsRemoveClient(clientID);
    }
    else if (opcode == WS_OPCODE_TEXT || opcode == WS_OPCODE_BINARY){
        if (callOnMessage != NULL)
            callOnMessage(clientID, data.substr(0, dataLength));
    }
    else {
        // unknown opcode
        return false;
    }

    return true;
}

bool webSocket::wsProcessClientFrame(int clientID){
    wsClient *client = wsClients[clientID];
    // store the time that data was last received from the client
    client->LastRecvTime = time(NULL);

    // check at least 6 bytes are set (first 2 bytes and 4 bytes for the mask key)
    if (client->FrameBuffer.size() < 6)
        return false;

    // fetch first 2 bytes of header
    unsigned char octet0 = client->FrameBuffer.at(0);
    unsigned char octet1 = client->FrameBuffer.at(1);

    unsigned char fin = octet0 & WS_FIN;
    unsigned char opcode = octet0 & 0x0f;

    //unsigned char mask = octet1 & WS_MASK;
    if (octet1 < 128)
        return false; // close socket, as no mask bit was sent from the client

    // fetch byte position where the mask key starts
    int seek = client->FrameBytesRead <= 125 ? 2 : (client->FrameBytesRead <= 65535 ? 4 : 10);

    // read mask key
    char maskKey[4];
    memcpy(maskKey, client->FrameBuffer.substr(seek, 4).c_str(), 4);

    seek += 4;

    // decode payload data
    string data;
    int clientFBSize = client->FrameBuffer.size();
    for (int i = seek; i < clientFBSize; i++){
        //data.append((char)(((int)client->FrameBuffer.at(i)) ^ maskKey[(i - seek) % 4]));
        char c = client->FrameBuffer.at(i);
        c = c ^ maskKey[(i - seek) % 4];
        data += c;
    }

    // check if this is not a continuation frame and if there is already data in the message buffer
    if (opcode != WS_OPCODE_CONTINUATION && client->MessageBufferLength > 0){
        // clear the message buffer
        client->MessageBufferLength = 0;
        client->MessageBuffer.clear();
    }

    // check if the frame is marked as the final frame in the message
    if (fin == WS_FIN){
        // check if this is the first frame in the message
        if (opcode != WS_OPCODE_CONTINUATION){
            // process the message
            return wsProcessClientMessage(clientID, opcode, data, client->FramePayloadDataLength);
        }
        else {
            // increase message payload data length
            client->MessageBufferLength += client->FramePayloadDataLength;
            // push frame payload data onto message buffer
            client->MessageBuffer.append(data);

            // process the message
            bool result = wsProcessClientMessage(clientID, client->MessageOpcode, client->MessageBuffer, client->MessageBufferLength);

            // check if the client wasn't removed, then reset message buffer and message opcode
            if (wsClients[clientID] != NULL){
                client->MessageBuffer.clear();
                client->MessageOpcode = 0;
                client->MessageBufferLength = 0;
            }

            return result;
        }
    }
    else {
        // check if the frame is a control frame, control frames cannot be fragmented
        if (opcode & 8)
            return false;

        // increase message payload data length
        client->MessageBufferLength += client->FramePayloadDataLength;

        // push frame payload data onto message buffer
        client->MessageBuffer.append(data);

        // if this is the first frame in the message, store the opcode
        if (opcode != WS_OPCODE_CONTINUATION) {
            client->MessageOpcode = opcode;
        }
    }

    return true;
}

bool webSocket::wsBuildClientFrame(int clientID, char *buffer, int bufferLength){
    wsClient *client = wsClients[clientID];
    // increase number of bytes read for the frame, and join buffer onto end of the frame buffer
    client->FrameBytesRead += bufferLength;
    client->FrameBuffer.append(buffer, bufferLength);

    // check if the length of the frame's payload data has been fetched, if not then attempt to fetch it from the frame buffer
    if (wsCheckSizeClientFrame(clientID) == true){
        // work out the header length of the frame
        int headerLength = (client->FramePayloadDataLength <= 125 ? 0 : (client->FramePayloadDataLength <= 65535 ? 2 : 8)) + 6;

        // check if all bytes have been received for the frame
        int frameLength = client->FramePayloadDataLength + headerLength;
        if (client->FrameBytesRead >= frameLength){
            char *nextFrameBytes;
            // check if too many bytes have been read for the frame (they are part of the next frame)
            int nextFrameBytesLength = client->FrameBytesRead - frameLength;
            if (nextFrameBytesLength > 0) {
                client->FrameBytesRead -= nextFrameBytesLength;
                nextFrameBytes = buffer + frameLength;
                client->FrameBuffer = client->FrameBuffer.substr(0, frameLength);
            }

            // process the frame
            bool result = wsProcessClientFrame(clientID);

            // check if the client wasn't removed, then reset frame data
            if (wsClients[clientID] != NULL){
                client->FramePayloadDataLength = -1;
                client->FrameBytesRead = 0;
                client->FrameBuffer.clear();
            }

            // if there's no extra bytes for the next frame, or processing the frame failed, return the result of processing the frame
            if (nextFrameBytesLength <= 0 || !result)
                return result;

            // build the next frame with the extra bytes
            return wsBuildClientFrame(clientID, nextFrameBytes, nextFrameBytesLength);
        }
    }

    return true;
}

bool webSocket::wsProcessClientHandshake(int clientID, char *buffer){
    // fetch headers and request line
    string buf(buffer);
    size_t sep = buf.find("\r\n\r\n");
    if (sep == string::npos)
        return false;

    string headers = buf.substr(0, sep);

    string request = headers.substr(0, headers.find("\r\n"));
    if (request.size() == 0)
        return false;

    string part;
    part = request.substr(0, request.find(" "));
    if (part.compare("GET") != 0 && part.compare("get") != 0 && part.compare("Get") != 0)
        return false;

    part = request.substr(request.rfind("/") + 1);
    if (atof(part.c_str()) < 1.1)
        return false;

    string host;
    string ws_key;
    string ws_version;
    headers = headers.substr(headers.find("\r\n") + 2);
    while (headers.size() > 0){
        request = headers.substr(0, headers.find("\r\n"));
        if (request.find(":") != string::npos){
            string key = request.substr(0, request.find(":"));
            if (key.find_first_not_of(" ") != string::npos)
                key = key.substr(key.find_first_not_of(" "));
            if (key.find_last_not_of(" ") != string::npos)
                key = key.substr(0, key.find_last_not_of(" ") + 1);

            string value = request.substr(request.find(":") + 1);
            if (value.find_first_not_of(" ") != string::npos)
                value = value.substr(value.find_first_not_of(" "));
            if (value.find_last_not_of(" ") != string::npos)
                value = value.substr(0, value.find_last_not_of(" ") + 1);

            if (key.compare("Host") == 0){
                host = value;
            }
            else if (key.compare("Sec-WebSocket-Key") == 0){
                ws_key = value;
            }
            else if (key.compare("Sec-WebSocket-Version") == 0){
                ws_version = value;
            }
        }
        if (headers.find("\r\n") == string::npos)
            break;
        headers = headers.substr(headers.find("\r\n") + 2);
    }

    if (host.size() == 0)
        return false;

    if (ws_key.size() == 0 || base64_decode(ws_key).size() != 16)
        return false;

    if (ws_version.size() == 0 || atoi(ws_version.c_str()) < 7)
        return false;

    unsigned char hash[20];
    ws_key.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    SHA1((unsigned char *)ws_key.c_str(), ws_key.size(), hash);
    string encoded_hash = base64_encode(hash, 20);

    string message = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ";
    message.append(encoded_hash);
    message.append("\r\n\r\n");

    int socket = wsClients[clientID]->socket;

    int left = message.size();
    do {
        int sent = send(socket, message.c_str(), message.size(), 0);
        if (sent == false) return false;

        left -= sent;
        if (sent > 0)
            message = message.substr(sent);
    }
    while (left > 0);

    return true;
}

bool webSocket::wsProcessClient(int clientID, char *buffer, int bufferLength){
    bool result;
    int clientArrSize = wsClients.size();
    if (clientID >= clientArrSize || wsClients[clientID] == NULL)
        return false;

    if (wsClients[clientID]->ReadyState == WS_READY_STATE_OPEN){
        // handshake completed
        result = wsBuildClientFrame(clientID, buffer, bufferLength);
    }
    else if (wsClients[clientID]->ReadyState == WS_READY_STATE_CONNECTING){
        // handshake not completed
        result = wsProcessClientHandshake(clientID, buffer);
        if (result){
            if (callOnOpen != NULL)
                callOnOpen(clientID);

            wsClients[clientID]->ReadyState = WS_READY_STATE_OPEN;
        }
    }
    else {
        // ready state is set to closed
        result = false;
    }

    return result;
}

int webSocket::wsGetNextClientID(){
    int i;
    int clientArrSize = wsClients.size();
    for (i = 0; i < clientArrSize; i++){
        if (wsClients[i] == NULL)
            break;
    }
    return i;
}

void webSocket::wsAddClient(int socket, in_addr ip){
    int clientID = wsGetNextClientID();
    wsClient *newClient = new wsClient(socket, ip);
    int clientArrSize = wsClients.size();
    if (clientID >= clientArrSize){
        wsClients.push_back(newClient);
    }
    else {
        wsClients[clientID] = newClient;
    }
    socketIDmap[socket] = clientID;
}

void webSocket::setOpenHandler(defaultCallback callback){
    callOnOpen = callback;
}

void webSocket::setCloseHandler(defaultCallback callback){
    callOnClose = callback;
}

void webSocket::setMessageHandler(messageCallback callback){
    callOnMessage = callback;
}

void webSocket::setPeriodicHandler(nullCallback callback){
    callPeriodic = callback;
}

void webSocket::startServer(int port)
{

    cout << "TCP server started" << endl;

    // Creating Socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    // Attach port
    int opt = 1;
    struct sockaddr_in address;
    int addresslen = sizeof(address);

    cout << "Creating socket" << endl;

    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1){
        perror("setsockopt() error!");
        exit(1);
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind on network
    if (bind(serverSocket, (struct sockaddr *)&address, sizeof(address)) == -1){
        perror("bind() error!");
        exit(1);
    }

    // Start listening for connections
    if (listen(serverSocket, 500000) == -1){
        perror("listen() error!");
        exit(1);
    }

    // Read mutliple connections
    struct epoll_event ev;
    struct epoll_event evlist[MAX];
    int epfd;
    struct sockaddr_in clientAddress;

    epfd = epoll_create1(0);
    ev.events = EPOLLIN;

    ev.data.fd = STDIN_FILENO; // for quit
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);

    ev.data.fd = serverSocket;
    epoll_ctl(epfd, EPOLL_CTL_ADD, serverSocket, &ev);

    int i;
    char buf[bfz];
    int nfds;
    int cfd;
    for (;;)
    {
        // epoll
        nfds = epoll_wait(epfd, evlist, MAX, -1);

        for (i = 0; i < nfds; i++)
        {
            if (evlist[i].data.fd == STDIN_FILENO)
            {
                fgets(buf, bfz - 1, stdin);
                if (!strcmp(buf, "quit") || !strcmp(buf, "exit"))
                {
                    close(serverSocket);
                    exit(0);
                }
            }
            else if (evlist[i].data.fd == serverSocket)
            {

                start = chrono::high_resolution_clock::now();

                // accept
                cfd = accept(serverSocket, (struct sockaddr *)&clientAddress, (socklen_t *)&addresslen);

                // epoll_ctl
                ev.events = EPOLLIN;
                ev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);

                if (cfd != -1){
                    /* add new client */
                    wsAddClient(cfd, clientAddress.sin_addr);
                    printf("New connection from %s on socket %d\n", inet_ntoa(clientAddress.sin_addr), cfd);
                }
            }
            else
            {
                int clientFd = evlist[i].data.fd;
                // handleClient(clientFd);

                int nbytes = recv(clientFd, buf, sizeof(buf), 0);
                        if (socketIDmap.find(clientFd) != socketIDmap.end()){
                            if (nbytes < 0)
                                wsSendClientClose(socketIDmap[clientFd], WS_STATUS_PROTOCOL_ERROR);
                            else if (nbytes == 0){
                                wsRemoveClient(socketIDmap[clientFd]);
                                epoll_ctl(epfd, EPOLL_CTL_DEL, clientFd, NULL);
                            }
                            else {
                                if (!wsProcessClient(socketIDmap[clientFd], buf, nbytes))
                                    wsSendClientClose(socketIDmap[clientFd], WS_STATUS_PROTOCOL_ERROR);
                            }
                        }
            }
        }

    }

    // closing the listening socket
    // shutdown(serverSocket, SHUT_RDWR);

}
