#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <string>
#include <stdio.h>
#include <assert.h>

#pragma comment(lib, "ws2_32.lib")

static void msg(const char* msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char* msg) {
    int err = WSAGetLastError(); // Use Winsock specific error retrieval
    fprintf(stderr, "[%d] %s\n", err, msg);
    exit(EXIT_FAILURE);
}

static int32_t read_full(SOCKET fd, char* buf, size_t n) {
    while (n > 0) {
        int rv = recv(fd, buf, (int)n, 0); // Cast 'n' to int as required by recv()
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, const char* buf, size_t n) {
    while (n > 0) {
        int rv = send(fd, buf, (int)n, 0);  // Cast 'n' to int
        if (rv <= 0) {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

const size_t k_max_msg = 4096;

static int32_t send_req(SOCKET fd, const std::vector<std::string>& cmd) {
    uint32_t len = 4;
    for (const std::string& s : cmd) {
        len += 4 + s.size();
    }
    if (len > k_max_msg) {
        return -1;
    }

    char wbuf[4 + k_max_msg];
    memcpy(&wbuf[0], &len, 4); 
    uint32_t n = (uint32_t)cmd.size();
    memcpy(&wbuf[4], &n, 4);
    size_t cur = 8;
    for (const std::string& s : cmd) {
        uint32_t p = (uint32_t)s.size();
        memcpy(&wbuf[cur], &p, 4);
        memcpy(&wbuf[cur + 4], s.data(), s.size());
        cur += 4 + s.size();
    }
    return write_all(fd, wbuf, 4 + len);
}

static int32_t read_res(SOCKET fd) { // Change int to SOCKET
    // 4 bytes header
    char rbuf[4 + k_max_msg + 1];
    int32_t err = read_full(fd, rbuf, 4);
    if (err) {
        if (WSAGetLastError() == 0) { // Use Winsock specific error check
            msg("EOF");
        } else {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4); 
    if (len > k_max_msg) {
        msg("too long");
        return -1;
    }

    err = read_full(fd, &rbuf[4], len);
    if (err) {
        msg("read() error");
        return err;
    }

    uint32_t rescode = 0;
    if (len < 4) {
        msg("bad response");
        return -1;
    }
    memcpy(&rescode, &rbuf[4], 4);
    printf("server says: [%u] %.*s\n", rescode, len - 4, &rbuf[8]);
    return 0;
}


int main(int argc, char** argv) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        die("WSAStartup failed");
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0); // Change int to SOCKET
    if (fd == INVALID_SOCKET) {
        die("socket()");
    }

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8000); // Assuming server is also using 1234
    addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Loopback address
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        die("connect");
    }

    std::vector<std::string> cmd;
    for (int i = 1; i < argc; ++i) {
        cmd.push_back(argv[i]);
    }
    int32_t err = send_req(fd, cmd);
    if (err) {
        goto L_DONE;
    }
    err = read_res(fd);
    if (err) {
        goto L_DONE;
    }

L_DONE:
    closesocket(fd); 
    WSACleanup();  
    return 0;
}
