#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h> 
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma comment(lib, "ws2_32.lib")

const size_t k_max_msg = 4096;

enum {
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2,
};

struct Conn {
    SOCKET fd = INVALID_SOCKET;
    uint32_t state = 0;
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

void die(const char* msg) {
    fprintf(stderr, "%s: %d\n", msg, WSAGetLastError());
}
   
static void conn_put(std::vector<Conn*>& fd2conn, struct Conn* conn) {
    if (fd2conn.size() <= (size_t)conn->fd) {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn*>& fd2conn, SOCKET fd) {
    struct sockaddr_in client_addr = {};
    int socklen = sizeof(client_addr);
    SOCKET connfd = accept(fd, (struct sockaddr*)&client_addr, &socklen);
    if (connfd == INVALID_SOCKET) {
        fprintf(stderr, "accept() error: %d\n", WSAGetLastError());
        return -1;
    }

    u_long nonBlockingMode = 1;
    ioctlsocket(connfd, FIONBIO, &nonBlockingMode);

    struct Conn* conn = (struct Conn*)malloc(sizeof(struct Conn));
    if (!conn) {
        closesocket(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    conn_put(fd2conn, conn);
    return 0;
}

static void state_req(Conn* conn);
static void state_res(Conn* conn);

static bool try_one_request(Conn* conn) {
    if (conn->rbuf_size < 4) {
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg) {
        fprintf(stderr, "too long\n");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size) {
        return false;
    }

    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    conn->state = STATE_RES;
    state_res(conn);

    return (conn->state == STATE_REQ);
}

static bool try_fill_buffer(Conn* conn) {
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    int rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = recv(conn->fd, (char*)&conn->rbuf[conn->rbuf_size], (int)cap, 0); // Cast cap to int
    } while (rv < 0 && WSAGetLastError() == WSAEINTR);
    if (rv < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return false;
    }
    if (rv < 0) {
        fprintf(stderr, "recv() error: %d\n", WSAGetLastError());
        conn->state = STATE_END;
        return false;
    }
    if (rv == 0) {
        if (conn->rbuf_size > 0) {
            fprintf(stderr, "unexpected EOF\n");
        } else {
            fprintf(stderr, "EOF\n");
        }
        conn->state = STATE_END;
        return false;
    }

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    while (try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

static void state_req(Conn* conn) {
    while (try_fill_buffer(conn)) {}
}

static bool try_flush_buffer(Conn* conn) {
    int rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = send(conn->fd, (const char*)&conn->wbuf[conn->wbuf_sent], (int)remain, 0); // Cast remain to int
    } while (rv < 0 && WSAGetLastError() == WSAEINTR);
    if (rv < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return false;
    }
    if (rv < 0) {
        fprintf(stderr, "send() error: %d\n", WSAGetLastError());
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent <= conn->wbuf_size);
    if (conn->wbuf_sent == conn->wbuf_size) {
        conn->state = STATE_REQ;
        conn->wbuf_sent = 0;
        conn->wbuf_size = 0;
        return false;
    }
    return true;
}

static void state_res(Conn* conn) {
    while (try_flush_buffer(conn)) {}
}




int main() {
   
   // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        die("WSAStartup failed");
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        die("socket failed");
    }

    int optval = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(int));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8000);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        die("bind failed");
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        die("listen failed");
    }

    // Vector to store active client connections
    std::vector<Conn*> connections;

    // Set the listening socket to non-blocking mode
    u_long nonBlockingMode = 1;
    if (ioctlsocket(listenSocket, FIONBIO, &nonBlockingMode) != 0) {
        die("ioctlsocket failed"); 
    }

    while (true) {
        // Polling setup
        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        FD_SET(listenSocket, &readfds); // Monitor the listening socket for incoming connections

        // Add client sockets to appropriate fd_sets
        for (Conn* conn : connections) {
            if (conn) {
                if (conn->state == STATE_REQ) {
                    FD_SET(conn->fd, &readfds);
                } else if (conn->state == STATE_RES) {
                    FD_SET(conn->fd, &writefds);
                }
            }
        }

        // Find the highest numbered file descriptor
        int max_fd = listenSocket;
        for (Conn* conn : connections) {
            if (conn && conn->fd > max_fd) {
                max_fd = conn->fd;
            }
        }

        // Wait for events (with a timeout of 1 second)
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int numEvents = select(max_fd + 1, &readfds, &writefds, NULL, &timeout);
        if (numEvents == SOCKET_ERROR) {
            die("select failed");
        }

        // Accept new connection
        if (FD_ISSET(listenSocket, &readfds)) {
            accept_new_conn(connections, listenSocket);
        }

        // Handle client connections
        for (auto it = connections.begin(); it != connections.end(); ) {
            Conn* conn = *it;
            if (conn) {
                if (FD_ISSET(conn->fd, &readfds)) {
                    state_req(conn);
                }
                if (FD_ISSET(conn->fd, &writefds)) {
                    state_res(conn);
                }
                if (conn->state == STATE_END) {
                    closesocket(conn->fd);
                    free(conn);
                    it = connections.erase(it); // Remove the closed connection
                    continue; // Skip incrementing the iterator
                }
            }
            ++it; // Increment the iterator only if the connection is not erased
        }
    }
    
    closesocket(listenSocket);
    WSACleanup();
    return 0;
}