#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static void die(const char *msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    exit(1);
}

static void do_something(SOCKET connfd) { 
    char rbuf[64] = {}; // receive buffer
    int n = recv(connfd, rbuf, sizeof(rbuf) - 1, 0); // receive message
    if (n == SOCKET_ERROR) {
        die("recv() failed");
    }
    printf("client says: %s\n", rbuf);

    const char wbuf[] = "Adarsh";
    if (send(connfd, wbuf, sizeof(wbuf), 0) == SOCKET_ERROR) {
        die("send() failed");
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        die("WSAStartup failed");
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        die("socket() failed");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // wildcard address 0.0.0.0

    //binding to adress
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        die("bind() failed");
    }   
    //listening to the socket
    if (listen(fd, SOMAXCONN) == SOCKET_ERROR) {
        die("listen() failed");
    }
    //accepting the connection
    while (true) {
        struct sockaddr_in client_addr = {}; // client's address
        int socklen = sizeof(client_addr); // size of client_addr
        SOCKET connfd = accept(fd, (struct sockaddr*)&client_addr, &socklen); // accept connection
        if (connfd == INVALID_SOCKET) {
            continue;   // error
        }

        do_something(connfd); // do something with the connection
        closesocket(connfd); // close the connection
    }

    closesocket(fd);//  close the listening socket when the server is done accepting connections.
    WSACleanup(); //cleaning up the socket library  

    return 0;
}