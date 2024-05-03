#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

static void die(const char *msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
    exit(1);
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
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        die("connect() failed");
    }

    const char msg[] = "Hello"; // message to send
    if (send(fd, msg, sizeof(msg), 0) == SOCKET_ERROR) {
        die("send() failed");
    }

    char rbuf[64] = {}; // receive buffer
    int n = recv(fd, rbuf, sizeof(rbuf) - 1, 0); // receive message
    if (n == SOCKET_ERROR) {
        die("recv() failed"); // error
    }

    printf("server says: %s\n", rbuf);

    closesocket(fd); // close the connection
    WSACleanup(); // clean up

    return 0;
}