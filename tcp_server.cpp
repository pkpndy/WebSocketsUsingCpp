#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <vector>
#include <sys/epoll.h>
#include <string>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <chrono>

#define DATA_BUFFER 4096
#define MAX_EVENTS 10000

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
    unsigned char *output = (unsigned char *)calloc(pl + 1, 1);
    const int ol = EVP_DecodeBlock(output, input, length);
    if (pl != ol)
    {
        fprintf(stderr, "Whoops, decode predicted %d but we got %d\n", pl, ol);
    }
    return output;
}

int create_tcp_server_socket() {
    const int opt = 1;

    /* Step1: create a TCP socket */
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == -1) {
        perror("Could not create socket");
        exit(EXIT_FAILURE);
    }

    printf("Created a socket with fd: %d\n", fd);

    /* Step2: set socket options */
    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) == -1) {

        perror("Could not set socket options");
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Initialize the socket address structure */
    /* Listen on port 8080 */
    struct sockaddr_in saddr;

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(8080);
    saddr.sin_addr.s_addr = INADDR_ANY;

    /* Step3: bind the socket to port 8080 */
    if (bind(fd, (struct sockaddr *)&saddr, sizeof(struct sockaddr_in)) == -1) {
        perror("Could not bind to socket");
        close(fd);
        exit(EXIT_FAILURE);
    }

    /* Step4: listen for incoming connections */
    /*
       The socket will allow a maximum of 1000 clients to be queued before
       refusing connection requests.
    */
    if (listen(fd, 1000) == -1) {
        perror("Could not listen on socket");
        close(fd);
        exit(EXIT_FAILURE);
    }

    return fd;
}

void accept_new_connection_request(int server_fd, int efd, struct epoll_event &ev) {
    struct sockaddr_in new_addr;
    int addrlen = sizeof(struct sockaddr_in);

    while (1) {
        /* Accept new connections */
        int connFD = accept(server_fd, (struct sockaddr*)&new_addr,
                          (socklen_t*)&addrlen);

        if (connFD == -1) {
            /* We have processed all incoming connections. */
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break;
            }
            else {
                perror ("accept");
                break;
            }
        }

        /* Make the new connection non blocking */
        fcntl(connFD, F_SETFL, O_NONBLOCK);

        /* Monitor new connection for read events in edge triggered mode */
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = connFD;

        /* Allow epoll to monitor new connection */
        if (epoll_ctl(efd, EPOLL_CTL_ADD, connFD, &ev) == -1) {
            perror("epoll_ctl: connFD");
            break;
        }
    }
}

std::vector<std::string> splitMsg(std::string &s, std::string delimiter) {
    size_t pos = 0;
    std::vector<std::string> parts;

    while ((pos = s.find(delimiter)) != std::string::npos) {
        std::string token = s.substr(0, pos);
        if (token.size() > 0) parts.push_back(token);
        s.erase(0, pos + delimiter.length());
    }

    return parts;
}

void sendWsResponse(int cfd, std::string acceptKey)
{
    std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade:websocket\r\nConnection:Upgrade\r\nSec-WebSocket-Accept:";
    response.append(acceptKey);
    response.append("\r\n\r\n");

    write(cfd, (char *)response.c_str(), response.length());

    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);


    std::cout << "Accepted connection in " << duration.count() << "\n";

}

void acceptWebSocketConnection(std::string ws_key, int cfd)
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

std::string giveValue(const std::string data, const std::string keyWord) {
    size_t connectionIndex = data.find(keyWord);
    if (connectionIndex != std::string::npos) {
        connectionIndex += keyWord.length();

        size_t endOfLineIndex = data.find("\r", connectionIndex);
        if (endOfLineIndex != std::string::npos) {
            std::string substringAfterConnection = data.substr(connectionIndex, endOfLineIndex - connectionIndex);
            return substringAfterConnection;
        }
    } else {
        std::cout << "Connection header not found" << std::endl;
    }
    return "";
}

void onSocketUpgrade(const std::string& data, int socket) {
    std::istringstream ss(data);
    std::vector<std::string> lines;
        if ((giveValue(data, "Connection: ")) == "Upgrade") {
            std::string webClientSocketKey  = giveValue(data, "Sec-WebSocket-Key: ");
            // Prepare handshake headers
            acceptWebSocketConnection(webClientSocketKey, socket);

            // // Convert string to const char* for send
            // const char* responseBuffer = response.c_str();

            // // Send the response
            // send(socket, responseBuffer, response.size(), 0);
        }
        else{
            std::cerr << "Not WebSocket Request\n";
        }
}

void recv_and_forward_message(int fd) {
    std::string remainder = "";

    // while (1) {
        char buf[DATA_BUFFER];
        int ret_data = recv(fd, buf, DATA_BUFFER, 0);

        if (ret_data > 0) {
            /* Read ret_data number of bytes from buf */
            std::string msg(buf, buf + ret_data);
            // msg = remainder + msg;

            // /* Parse and split incoming bytes into individual messages */
            // std::vector<std::string> parts = splitMsg(msg, "<EOM>");
            // remainder = msg;

            // for (size_t i = 0; i < parts.size(); i++) {
            //     std::cout << parts[i] << std::endl;  // Print each part
            // }
            // std::cout << msg << std::endl;
            onSocketUpgrade(msg, fd);

        }
        // else {
        //     /* Stopped sending new data */
        //     break;
        // }
    // }
}

int main(int argc, char const* argv[])
{
    int connFD;
    ssize_t valread;
    const std::string WEBSOCKET_MAGIC_STRING_KEY = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* Get the socket server fd */
int server_fd = create_tcp_server_socket();

/* Make the socket non blocking, will not wait for connection
    indefinitely */
fcntl(server_fd, F_SETFL, O_NONBLOCK);

if (server_fd == -1) {
    perror ("Could not create socket");
    exit(EXIT_FAILURE);
}

struct epoll_event ev, events[MAX_EVENTS];

/* Create epoll instance */
int efd = epoll_create1 (0);

if (efd == -1) {
    perror ("epoll_create");
    exit(EXIT_FAILURE);
}

ev.data.fd = server_fd;

/* Interested in read's events using edge triggered mode */
ev.events = EPOLLIN | EPOLLET;

/* Allow epoll to monitor the server_fd socket */
if (epoll_ctl (efd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
    perror ("epoll_ctl");
    exit(EXIT_FAILURE);
}
    while (1) {
    /* Returns only sockets for which there are events */
    int nfds = epoll_wait(efd, events, MAX_EVENTS, -1);

    if (nfds == -1) {
        perror("epoll_wait");
        exit(EXIT_FAILURE);
    }

    /* Iterate over sockets only having events */
    for (int i = 0; i < nfds; i++) {
        int fd = events[i].data.fd;
        
        if (fd == server_fd) {
            /* New connection request received */
            accept_new_connection_request(fd, efd, ev);
        }
        
        else if ((events[i].events & EPOLLERR) || 
                        (events[i].events & EPOLLHUP) ||
                        (!(events[i].events & EPOLLIN))) {

            /* Client connection closed */
            close(fd);
        }
        
        else {
            /* Received data on an existing client socket */
            recv_and_forward_message(fd);
        }
    }
}

    // closing the connected socket
    close(connFD);
    // closing the listening socket
    close(server_fd);
    return 0;
}
