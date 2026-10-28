#include "Gvtcier.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cstdio>
#include <chrono>

// 网络层最底下的一层：只负责裸 UDP 套接字的创建、收发、非阻塞与地址转换。
// 上层（网络节点/网络同步）不直接碰 winsock，全部经由这里。
// Socket_Init 用引用计数支持多处同时使用，最后一个使用者退出时才真正 WSACleanup。
namespace gvnet {

static int s_ref = 0;   // WSAStartup 引用计数

// 单调时钟，单位秒，从第一次调用开始计时。用 steady_clock 避免系统时间被改导致时序错乱。
double Socket_Now() {
    using namespace std::chrono;
    static steady_clock::time_point t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

// 引用计数式初始化：第一个调用者负责 WSAStartup，失败直接返回 false
bool Socket_Init() {
    if (s_ref == 0) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false;
    }
    ++s_ref;
    return true;
}

void Socket_Shutdown() {
    if (s_ref > 0) {
        --s_ref;
        if (s_ref == 0) WSACleanup();
    }
}

uint32_t Socket_LocalIp() {
    char host[256];
    host[0] = 0;
    if (gethostname(host, (int)sizeof(host) - 1) != 0) return 0;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return 0;
    uint32_t ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return ip;
}

Socket::~Socket() { close(); }

bool Socket::open(uint16_t port, bool reuse) {
    close();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    if (reuse) {
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, (int)sizeof(on));
    }
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(port);
    if (bind(s, (sockaddr*)&a, (int)sizeof(a)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    int buf = 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, (int)sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, (int)sizeof(buf));
    sock_ = (uintptr_t)s;
    if (port == 0) {
        sockaddr_in got;
        int glen = (int)sizeof(got);
        if (getsockname(s, (sockaddr*)&got, &glen) == 0) port = ntohs(got.sin_port);
    }
    port_ = port;
    return true;
}

void Socket::close() {
    if (sock_ != ~(uintptr_t)0) {
        closesocket((SOCKET)sock_);
        sock_ = ~(uintptr_t)0;
        port_ = 0;
    }
}

bool Socket::valid() const { return sock_ != ~(uintptr_t)0; }

int Socket::sendTo(const Addr& to, const void* data, int len) {
    if (!valid() || len <= 0) return 0;
    sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(to.ip);
    a.sin_port = htons(to.port);
    int n = ::sendto((SOCKET)sock_, (const char*)data, len, 0, (sockaddr*)&a, (int)sizeof(a));
    return n <= 0 ? 0 : n;
}

int Socket::recvFrom(Addr& from, void* buf, int cap) {
    if (!valid()) return 0;
    sockaddr_in a;
    int alen = (int)sizeof(a);
    memset(&a, 0, sizeof(a));
    int n = ::recvfrom((SOCKET)sock_, (char*)buf, cap, 0, (sockaddr*)&a, &alen);
    if (n <= 0) return 0;
    from.ip = ntohl(a.sin_addr.s_addr);
    from.port = ntohs(a.sin_port);
    return n;
}

bool Socket::setBroadcast(bool on) {
    if (!valid()) return false;
    BOOL v = on ? TRUE : FALSE;
    return setsockopt((SOCKET)sock_, SOL_SOCKET, SO_BROADCAST, (const char*)&v, (int)sizeof(v)) == 0;
}

Addr Socket::localAddr() const {
    Addr r;
    r.ip = Socket_LocalIp();
    r.port = port_;
    return r;
}

Addr Socket::resolve(const char* host, uint16_t port) {
    Addr r;
    r.port = port;
    if (!host || !host[0]) return r;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return r;
    r.ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return r;
}

Addr Socket::make(uint32_t ip, uint16_t port) {
    Addr r;
    r.ip = ip;
    r.port = port;
    return r;
}

uint32_t Socket::broadcastIp() { return 0xFFFFFFFFu; }

bool Socket::same(const Addr& a, const Addr& b) { return a.ip == b.ip && a.port == b.port; }

}
