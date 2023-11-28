#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

const int FIN_BIT = 0x80;
const int OPCODE_MASK = 0x0F;
const int MASK_BIT = 0x80;
const int PAYLOAD_LEN_MASK = 0x7F;
const int PAYLOAD_LEN_EXT16 = 126;
const int PAYLOAD_LEN_EXT64 = 127;
const int MASKING_KEY_LEN = 4;

// Function to process received WebSocket frames
void processWebSocketFrames(int socketFD) {
    char buffer[4096]; // Adjust buffer size as needed
    int bytesRead = recv(socketFD, buffer, sizeof(buffer), 0);

    if (bytesRead <= 0) {
        // Handle errors or closed connection
        return;
    }

    // Parse the WebSocket frame header
    int index = 0;
    int opcode = buffer[index] & OPCODE_MASK;
    bool isFinalFrame = buffer[index] & FIN_BIT;
    bool isMasked = buffer[index + 1] & MASK_BIT;

    int payloadLen = buffer[index + 1] & PAYLOAD_LEN_MASK;
    index += 2;

    if (payloadLen == PAYLOAD_LEN_EXT16) {
        // Handle extended payload length (16-bit)
        // Extract payload length from the next 2 bytes
        // Update index accordingly
    } else if (payloadLen == PAYLOAD_LEN_EXT64) {
        // Handle extended payload length (64-bit)
        // Extract payload length from the next 8 bytes
        // Update index accordingly
    }

    // Masking key (if present)
    char maskingKey[MASKING_KEY_LEN] = {};
    if (isMasked) {
        // Extract masking key from the next 4 bytes
        // Update index accordingly
    }

    // Get payload data
    char payloadData[4096]; // Adjust size based on payload length
    memcpy(payloadData, buffer + index, payloadLen);

    if (isMasked) {
        // Unmask payload data using the masking key
        for (int i = 0; i < payloadLen; ++i) {
            payloadData[i] ^= maskingKey[i % MASKING_KEY_LEN];
        }
    }

    // Handle the received message based on the opcode (e.g., text, binary)
    if (opcode == 0x01) { // Text frame
        std::string message(payloadData, payloadLen);
        // Process and handle the received text message
        std::cout << "Received text message: " << message << std::endl;
    }

    // Handle other opcodes (e.g., binary frames, control frames) as needed
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

// Assuming you have established a TCP connection and have a socketFD for communication

// Read and parse WebSocket frames
// void processWebSocketFrames(int socketFD) {
//     char buffer[4096]; // Adjust buffer size as needed
//     int bytesRead = recv(socketFD, buffer, sizeof(buffer), 0);

//     // Check for errors or connection closure
//     if (bytesRead <= 0) {
//         // Handle errors or closed connection
//         return;
//     }

//     // Parse WebSocket frames
//     // Assuming a simplified scenario without masking, fragmentation, etc.
//     // This example assumes handling a single-frame message

//     // Parse the WebSocket frame
//     bool isFinalFrame = (buffer[0] & 0x80) != 0; // Check if it's the final frame
//     int opcode = buffer[0] & 0x0F; // Extract the opcode

//     if (opcode == 0x01) { // Text frame
//         int payloadLength = buffer[1] & 0x7F; // Extract payload length
//         int payloadOffset = 2; // Start of payload data

//         if (payloadLength == 126) {
//             // Extended payload length (16-bit)
//             payloadLength = (buffer[2] << 8) | buffer[3];
//             payloadOffset = 4;
//         } else if (payloadLength == 127) {
//             // Extended payload length (64-bit)
//             // Handle extended length for larger payloads
//             // ...
//         }

//         // Extract message payload
//         std::string message;
//         for (int i = payloadOffset; i < bytesRead; ++i) {
//             message += buffer[i];
//         }

//         // Handle the received message (text frame)
//         std::cout << "Received text message: " << message << std::endl;
//     } else if (opcode == 0x02) { // Binary frame
//         // Handle binary frame (similar logic as text frame)
//     }

//     // Handle continuation frames, control frames, etc. (if required)
//     // ...

//     // Continue reading and processing WebSocket frames (loop or recursion)
//     processWebSocketFrames(socketFD);
// }


void handleClient(int cfd)
{

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);


    std::cout << "Received handshake in " << duration.count() << "\n";


    // read
    char buf[BUFSIZ];

    int buflen = recv(cfd, buf, BUFSIZ - 1, 0);
    if (buflen > 0)
    {

        // do something with data
        buf[buflen] = '\0';
        std::string msg = buf;
        // const char* charArray = "your char array";
        if ((buf[0] & 0x0F) == 0x01) { // Text frame
        int payloadLength = buf[1] & 0x7F; // Extract payload length
        int payloadOffset = 2; // Start of payload data

        std::string message(buf + payloadOffset, payloadLength);
        for (int i = payloadOffset; i < buflen; ++i) {
            message += buf[i];
        }

        // Handle the received message (text frame)
        std::cout << "Received text message: " << message << std::endl;
        }
        acceptWsConnection(cfd, buf);

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
