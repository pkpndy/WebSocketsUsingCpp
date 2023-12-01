#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <thread>
#include <chrono>


#define MAX 500000
#define PORT 8080

const char SEVEN_BITS_INTEGER_MARKER = 125;
const char SIXTEEN_BITS_INTEGER_MARKER = 126;
const char SIXTYFOUR_BITS_INTEGER_MARKER = 127;

const int MAXIMUM_SIXTEEN_BITS_INTEGER = 65536;
const int MASK_KEY_BYTES_LENGTH = 4;
const char OPCODE_TEXT = 0x01;

const char FIRST_BIT = 128;

auto start = std::chrono::high_resolution_clock::now();


char *base64_encode(const unsigned char *input, int length)
{
    const auto pl = 4 * ((length + 2) / 3);
    auto output = reinterpret_cast<char *>(calloc(pl + 1, 1)); //+1 for the terminating null that EVP_EncodeBlock adds on
    const auto ol = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(output), input, length);
    if (pl != ol)
    {
        std::cerr << "Whoops, encode predicted " << pl << " but we got " << ol << "\n";
    }
    return output;
}

unsigned char *base64_decode(const unsigned char *input, int length)
{
    const int pl = 3 * length / 4;
    std::cout <<"length:"<<length<<std::endl;
    unsigned char *output = (unsigned char *)calloc(pl + 1, 1);
    const int ol = EVP_DecodeBlock(output, input, length);
    if (pl != ol)
    {
        fprintf(stderr, "Whoops, decode predicted %d but we got %d\n", pl, ol);
    }
    return output;
}

void sendWsResponse(int cfd, char *acceptKey)
{
    std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade:websocket\r\nConnection:upgrade\r\nSec-WebSocket-Accept:";
    response.append(acceptKey);
    response.append("\r\n\n");

    write(cfd, (char *)response.c_str(), response.length());

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);


    std::cout << "Accepted connection in " << duration.count() << "\n";

}

void sendHttpResponse(int cfd)
{

    char msg[] = "HTTP/1.1 200 OK\nContent-Type:text/html; charset=UTF-8\n\nHello world";
    write(cfd, msg, sizeof(msg));

    close(cfd);
}

void acceptWebSocketConnection(int cfd, std::string ws_key)
{

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);


    std::cout << "Accepting connection " << duration.count() << "\n";

    ws_key.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

    unsigned char inputbuf[ws_key.length()];
    std::copy(ws_key.begin(), ws_key.end(), inputbuf);
    inputbuf[ws_key.length()] = 0;

    unsigned char obuf[20];

    SHA1(inputbuf, strlen((char *)inputbuf), obuf);
    char *hash = base64_encode(obuf, 20);

    sendWsResponse(cfd, hash);
}

bool acceptWsConnection(int clientSocket, char *buffer)
{
    // fetch headers and request line
    std::string buf(buffer);
    size_t sep = buf.find("\r\n\r\n");
    if (sep == std::string::npos)
        return false;

    std::string headers = buf.substr(0, sep);

    std::string request = headers.substr(0, headers.find("\r\n"));
    if (request.size() == 0)
        return false;

    std::string part;
    part = request.substr(0, request.find(" "));
    if (part.compare("GET") != 0 && part.compare("get") != 0 && part.compare("Get") != 0)
        return false;

    part = request.substr(request.rfind("/") + 1);
    if (atof(part.c_str()) < 1.1)
        return false;

    std::string host;
    std::string ws_key;
    std::string ws_version;
    headers = headers.substr(headers.find("\r\n") + 2);

    while (headers.size() > 0)
    {
        request = headers.substr(0, headers.find("\r\n"));
        if (request.find(":") != std::string::npos)
        {
            std::string key = request.substr(0, request.find(":"));
            if (key.find_first_not_of(" ") != std::string::npos)
                key = key.substr(key.find_first_not_of(" "));
            if (key.find_last_not_of(" ") != std::string::npos)
                key = key.substr(0, key.find_last_not_of(" ") + 1);

            std::string value = request.substr(request.find(":") + 1);
            if (value.find_first_not_of(" ") != std::string::npos)
                value = value.substr(value.find_first_not_of(" "));
            if (value.find_last_not_of(" ") != std::string::npos)
                value = value.substr(0, value.find_last_not_of(" ") + 1);

            if (key.compare("Host") == 0)
            {
                host = value;
            }
            else if (key.compare("Sec-WebSocket-Key") == 0)
            {
                ws_key = value;
            }
            else if (key.compare("Sec-WebSocket-Version") == 0)
            {
                ws_version = value;
            }
        }
        if (headers.find("\r\n") == std::string::npos)
            break;
        headers = headers.substr(headers.find("\r\n") + 2);
    }

    if (ws_key.empty())
    {
        sendHttpResponse(clientSocket);
    }
    else
    {
        acceptWebSocketConnection(clientSocket, ws_key);
    }

    return true;
}

bool isConnectionUpgrade()
{
    return false;
}

std::string prepareMessage(const std::string& message) {
    std::vector<char> msg(message.begin(), message.end());
    int messageSize = msg.size();

    std::vector<char> dataFrameBuffer(4096);

    const char firstByte = 0x80 | OPCODE_TEXT;
    if (messageSize <= SEVEN_BITS_INTEGER_MARKER) {
        dataFrameBuffer.push_back(firstByte);
        dataFrameBuffer.push_back(static_cast<char>(messageSize));
    } else if (messageSize <= MAXIMUM_SIXTEEN_BITS_INTEGER) {
        std::vector<char> target(4, 0);
        target[0] = firstByte;
        target[1] = SIXTEEN_BITS_INTEGER_MARKER;

        target[2] = static_cast<char>((messageSize >> 8) & 0xFF);
        target[3] = static_cast<char>(messageSize & 0xFF);

        dataFrameBuffer = target;
    } else {
        throw std::runtime_error("Message too long!");
    }

    int totalLength = dataFrameBuffer.size() + messageSize;
    std::string dataFrameResponse(dataFrameBuffer.begin(), dataFrameBuffer.end());
    dataFrameResponse += std::string(msg.begin(), msg.end());

    return dataFrameResponse;
}

void sendMessage(const std::string& msg, const std::string& socket) {
    std::string data = prepareMessage(msg);
    // Simulate socket write
    std::cout << "Sending message: " << data << " to socket: " << socket << std::endl;
}

std::string unmask(const std::vector<char>& encodedBuffer, const std::string& maskKey) {
    std::vector<char> finalBuffer = encodedBuffer;

    for (size_t index = 0; index < encodedBuffer.size(); ++index) {
        finalBuffer[index] = encodedBuffer[index] ^ maskKey[index % MASK_KEY_BYTES_LENGTH];
    }

    std::string decoded(finalBuffer.begin(), finalBuffer.end());
    return decoded;
}

void onSocketReadable(const std::string& socketData) {
    // Simulate incoming data from socket
    std::vector<char> socketBuffer(socketData.begin(), socketData.end());

    // Simulate parsing of WebSocket frame
    char opcode = socketBuffer[0];
    char markerAndPayloadLength = socketBuffer[1];

    char lengthIndicatorInBits = markerAndPayloadLength - FIRST_BIT;

    int messageLength = 0;
    if (lengthIndicatorInBits <= SEVEN_BITS_INTEGER_MARKER) {
        messageLength = lengthIndicatorInBits;
    } else if (lengthIndicatorInBits == SIXTEEN_BITS_INTEGER_MARKER) {
        messageLength = (socketBuffer[2] << 8) | socketBuffer[3];
    } else {
        throw std::runtime_error("Message too long!");
    }

    std::string maskKey(socketBuffer.begin() + 2, socketBuffer.begin() + 6);
    std::vector<char> encoded(socketBuffer.begin() + 6, socketBuffer.begin() + 6 + messageLength);

    // Simulated unmasking and decoding
    std::string decoded = unmask(encoded, maskKey);

    std::cout << "Received message: " << decoded << std::endl;

    // Simulated sending a response message
    std::string msg = "Response: " + decoded;
    sendMessage(msg, "client_socket");
}

void handleClient(int cfd)
{

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);


    std::cout << "Received handshake in " << duration.count() << "\n";


    // read
    char buf[BUFSIZ];
    
    int buflen = read(cfd, buf, BUFSIZ - 1);
    if (buflen > 0)
    { 
        
        // do something with data
        buf[buflen] = '\0';
        std::string msg = buf;
        acceptWsConnection(cfd, buf);
    onSocketReadable(buf);

    }else{
        close(cfd);
    }
}

int main()
{

    std::cout << "TCP server started" << std::endl;

    // Creating Socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    // Attach port
    int opt = 1;
    struct sockaddr_in address;
    int addresslen = sizeof(address);

    std::cout << "Creating socket" << std::endl;

    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind on network
    bind(serverSocket, (struct sockaddr *)&address, sizeof(address));

    // Start listening for connections
    listen(serverSocket, 500000);

    // Read mutliple connections
    struct epoll_event ev;
    struct epoll_event evlist[MAX];
    int ret;
    int epfd;
    struct sockaddr_in clientAddress;

    epfd = epoll_create1(0);
    ev.events = EPOLLIN;

    ev.data.fd = STDIN_FILENO; // for quit
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);

    ev.data.fd = serverSocket;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, serverSocket, &ev);

    int i;
    char buf[BUFSIZ];
    int buflen;
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

                fgets(buf, BUFSIZ - 1, stdin);
                if (!strcmp(buf, "quit") || !strcmp(buf, "exit"))
                {
                    close(serverSocket);
                    exit(0);
                }
            }
            else if (evlist[i].data.fd == serverSocket)
            {

                start = std::chrono::high_resolution_clock::now();

                // accept
                cfd = accept(serverSocket, (struct sockaddr *)&clientAddress, (socklen_t *)&addresslen);
                printf("a user connected\n");

                // epoll_ctl
                ev.events = EPOLLIN;
                ev.data.fd = cfd;
                ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
            }
            else
            {

                int clientFd = evlist[i].data.fd;
                handleClient(clientFd);
                // std::thread clientThread(handleClient, clientFd);
                // clientThread.detach();
            }
        }
    }

    // closing the listening socket
    shutdown(serverSocket, SHUT_RDWR);

    return 0;
}
