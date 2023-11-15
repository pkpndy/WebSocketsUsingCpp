// Client side C/C++ program to demonstrate Socket
// programming
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define PORT 8080
#define MAX_LIMIT 4096

void recv_messages(int server_fd) {
    int ret_data;
    std::string remainder = "";

    while (1) {
        char buf[MAX_LIMIT];
        ret_data = recv(server_fd, buf, MAX_LIMIT, 0);

        if (ret_data > 0) {
            std::string msg(buf, buf+ret_data);
            msg = remainder + msg;
            std::vector<std::string> parts = split(msg, "<EOM>");
            remainder = msg;

            for (int i = 0; i < parts.size(); i++) {
                std::cout << parts[i] << std::endl;
            }
        }
        else {
            remainder = "";
        }
    }
}

std::vector<std::string> split(std::string &s, std::string delimiter) {
    size_t pos = 0;
    std::vector<std::string> parts;

    while ((pos = s.find(delimiter)) != std::string::npos) {
        std::string token = s.substr(0, pos);
        if (token.size() > 0) parts.push_back(token);
        s.erase(0, pos + delimiter.length());
    }
    
    return parts;
}

int main(int argc, char const* argv[])
{
	int status, valread, client_fd;
	struct sockaddr_in serv_addr;
	// char* hello = "Hello from client";
	// char buffer[1024] = { 0 };
	if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Socket creation error \n");
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);

	// Convert IPv4 and IPv6 addresses from text to binary
	// form
	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)
		<= 0) {
		printf(
			"\nInvalid address/ Address not supported \n");
		return -1;
	}

	if ((status
		= connect(client_fd, (struct sockaddr*)&serv_addr,
				sizeof(serv_addr)))
		< 0) {
		printf("\nConnection Failed \n");
		return -1;
	}
	printf("The socket is now connected\n");

	std::thread t(recv_messages, fd);

	char msg[MAX_LIMIT];

	while (1) {
        fgets(msg, MAX_LIMIT, stdin);
        status = send(client_fd, msg, strlen(msg), 0);
    }

    t.join();

	
	printf("Hello message sent\n");
	// valread = read(client_fd, buffer, 1024 - 1); // subtract 1 for the null terminator at the end
	// printf("%s\n", buffer);

	// closing the connected socket
	close(client_fd);
	return 0;
}

