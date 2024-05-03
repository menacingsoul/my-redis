#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib") // Winsock Library

static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = WSAGetLastError();
    fprintf(stderr, "[%d] %s\n", err, msg);
    exit(EXIT_FAILURE);
}

const size_t k_max_msg = 4096;

static int32_t read_full(SOCKET fd, char *buf, size_t n) {
    while (n > 0) {
        int rv = recv(fd, buf, n, 0);
        if (rv <= 0) {
            return -1;  // error, or unexpected EOF
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, const char *buf, size_t n) {
    while (n > 0) {
        int rv = send(fd, buf, n, 0);
        if (rv <= 0) {
            return -1;  // error
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t one_request(SOCKET connfd) {
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    int32_t err = read_full(connfd, rbuf, 4);
    if (err) {
        msg("read() error");
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);  // assume little endian
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(connfd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    // do something
    rbuf[4 + len] = '\0';
    printf("client says: %s\n", &rbuf[4]);

    // reply using the same protocol
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    len = (uint32_t)strlen(reply);
    memcpy(wbuf, &len, 4);
    memcpy(&wbuf[4], reply, len);
    return write_all(connfd, wbuf, 4 + len);
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2),&wsa) != 0)
    {
        die("Failed. Error Code : ");
    }

    SOCKET fd;
    if((fd = socket(AF_INET , SOCK_STREAM , 0 )) == INVALID_SOCKET)
    {
        die("Could not create socket : ");
    }

    // this is needed for most server applications
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000);
    addr.sin_addr.s_addr = INADDR_ANY;
    if( bind(fd ,(struct sockaddr *)&addr , sizeof(addr)) == SOCKET_ERROR)
    {
        die("Bind failed with error code : ");
    }

    // listen
    listen(fd , SOMAXCONN);

    while (true) {
        // accept
        struct sockaddr_in client_addr = {};
        int socklen = sizeof(struct sockaddr_in);
        SOCKET connfd;
        if((connfd = accept(fd , (struct sockaddr *)&client_addr, &socklen)) == INVALID_SOCKET)
        {
            continue;   // error
        }

        while (true) {
            // here the server only serves one client connection at once
            int32_t err = one_request(connfd);
            if (err) {
                break;
            }
        }
        closesocket(connfd);
    }

    WSACleanup();

    return 0;
}