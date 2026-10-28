#pragma once
#include "bicu/core/export.h"
#include <cstdint>

namespace gvnet {

struct Addr {
    uint32_t ip = 0;
    uint16_t port = 0;
};

GVTCIER_API bool Socket_Init();
GVTCIER_API void Socket_Shutdown();
GVTCIER_API uint32_t Socket_LocalIp();
GVTCIER_API double Socket_Now();

class GVTCIER_API Socket {
public:
    ~Socket();
    bool open(uint16_t port, bool reuse);
    void close();
    bool valid() const;
    int sendTo(const Addr& to, const void* data, int len);
    int recvFrom(Addr& from, void* buf, int cap);
    bool setBroadcast(bool on);
    uint16_t localPort() const { return port_; }
    Addr localAddr() const;
    static Addr resolve(const char* host, uint16_t port);
    static Addr make(uint32_t ip, uint16_t port);
    static uint32_t broadcastIp();
    static bool same(const Addr& a, const Addr& b);

private:
    uintptr_t sock_ = ~(uintptr_t)0;
    uint16_t port_ = 0;
};

}
