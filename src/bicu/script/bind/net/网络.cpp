#include <winsock2.h>
#include <ws2tcpip.h>
#include "Gvtcier.h"

// 脚本绑定：网络。脚本可创建 TCP/UDP 套接字做自定义通信（与引擎的 gvnet 对象同步无关）。
// 套接字以 userdata 形式交给脚本持有，脚本侧的垃圾回收会触发关闭。

// 脚本能拿到的套接字句柄。s 为 INVALID_SOCKET 表示已关闭
struct NetSock {
    SOCKET s = INVALID_SOCKET;
    bool listener = false;
};

static std::map<int, NetSock> g_netSocks;
static int g_netNextId = 1;

static bool netStartup() {
    static bool init = false;
    if (init) return true;
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return false;
    init = true;
    return true;
}

static NetSock* netSock(int id) {
    auto it = g_netSocks.find(id);
    if (it == g_netSocks.end()) return nullptr;
    return &it->second;
}

static int g_net_connect(gvts_State* L) {
    if (!netStartup()) { gvts_pushnil(L); return 1; }
    const char* host = gvtsG_checkstring(L, 1);
    int port = (int)gvtsG_checknumber(L, 2);
    if (!host || !host[0] || port <= 0 || port > 65535) { gvts_pushnil(L); return 1; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { gvts_pushnil(L); return 1; }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portStr[16];
    _snprintf(portStr, sizeof(portStr), "%d", port);
    addrinfo* result = nullptr;
    if (getaddrinfo(host, portStr, &hints, &result) != 0 || !result) {
        closesocket(s);
        gvts_pushnil(L);
        return 1;
    }
    bool ok = connect(s, result->ai_addr, (int)result->ai_addrlen) == 0;
    freeaddrinfo(result);
    if (!ok) { closesocket(s); gvts_pushnil(L); return 1; }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    NetSock ns;
    ns.s = s;
    ns.listener = false;
    int id = g_netNextId++;
    g_netSocks[id] = ns;
    gvts_pushnumber(L, (double)id);
    return 1;
}

static int g_net_listen(gvts_State* L) {
    if (!netStartup()) { gvts_pushnil(L); return 1; }
    int port = (int)gvtsG_checknumber(L, 1);
    if (port <= 0 || port > 65535) { gvts_pushnil(L); return 1; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { gvts_pushnil(L); return 1; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(s, 8) != 0) {
        closesocket(s);
        gvts_pushnil(L);
        return 1;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    NetSock ns;
    ns.s = s;
    ns.listener = true;
    int id = g_netNextId++;
    g_netSocks[id] = ns;
    gvts_pushnumber(L, (double)id);
    return 1;
}

static int g_net_accept(gvts_State* L) {
    int id = (int)gvtsG_checknumber(L, 1);
    NetSock* n = netSock(id);
    if (!n || !n->listener) { gvts_pushnil(L); return 1; }
    SOCKET c = accept(n->s, nullptr, nullptr);
    if (c == INVALID_SOCKET) { gvts_pushnil(L); return 1; }
    u_long nb = 1;
    ioctlsocket(c, FIONBIO, &nb);
    NetSock ns;
    ns.s = c;
    ns.listener = false;
    int cid = g_netNextId++;
    g_netSocks[cid] = ns;
    gvts_pushnumber(L, (double)cid);
    return 1;
}

static int g_net_send(gvts_State* L) {
    int id = (int)gvtsG_checknumber(L, 1);
    const char* text = gvtsG_checkstring(L, 2);
    NetSock* n = netSock(id);
    if (!n || n->listener || !text) { gvts_pushboolean(L, 0); return 1; }
    int len = (int)strlen(text);
    int sent = send(n->s, text, len, 0);
    gvts_pushboolean(L, sent == len ? 1 : 0);
    return 1;
}

static int g_net_recv(gvts_State* L) {
    int id = (int)gvtsG_checknumber(L, 1);
    NetSock* n = netSock(id);
    if (!n || n->listener) { gvts_pushnil(L); return 1; }
    char buf[65536];
    int got = recv(n->s, buf, sizeof(buf), 0);
    if (got <= 0) {
        if (got == 0) {
            closesocket(n->s);
            g_netSocks.erase(id);
        }
        gvts_pushnil(L);
        return 1;
    }
    gvts_pushlstring(L, buf, got);
    return 1;
}

static int g_net_close(gvts_State* L) {
    int id = (int)gvtsG_checknumber(L, 1);
    auto it = g_netSocks.find(id);
    if (it == g_netSocks.end()) { gvts_pushboolean(L, 0); return 1; }
    closesocket(it->second.s);
    g_netSocks.erase(it);
    gvts_pushboolean(L, 1);
    return 1;
}

void Gvtcier_Bind_Net(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"网络", u8"连接_服务器", u8"以客户端连接 TCP 服务器：主机, 端口；返回连接编号，失败 nil", g_net_connect);
    regCap(L, ctx, u8"网络", u8"监听_端口", u8"开 TCP 服务器监听：端口；返回监听编号，失败 nil", g_net_listen);
    regCap(L, ctx, u8"网络", u8"接受_连接", u8"取一个新接入的客户端：监听编号；无则 nil", g_net_accept);
    regCap(L, ctx, u8"网络", u8"发送_文本", u8"向连接发送文本：连接编号, 文本", g_net_send);
    regCap(L, ctx, u8"网络", u8"接收_文本", u8"从连接收取文本（非阻塞）：连接编号；无数据 nil", g_net_recv);
    regCap(L, ctx, u8"网络", u8"断开_连接", u8"关闭连接或监听：编号", g_net_close);
}
