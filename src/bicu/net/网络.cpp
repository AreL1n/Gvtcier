#include "Gvtcier.h"
#include <chrono>
#include <cstring>

// 网络模块的对外门面。底层分三块：
//   网络套接字.cpp  裸 UDP 收发与端点抽象
//   网络节点.cpp    房间、对等端、握手、心跳
//   网络同步.cpp    场景对象的属性复制与插值
// 本文件只做三件事：持有全局端点与复制器、把端点回调转给复制器、转出少量高层接口。
namespace {

gvnet::Endpoint g_ep;            // 全局 UDP 端点，负责实际收发包
gvnet::Replicator g_rep;         // 全局复制器，负责对象状态同步
uint16_t g_defaultPort = gvnet::kDefaultPort;  // 未显式指定端口时使用的默认端口
bool g_inited = false;           // 防止重复初始化，重复调用会先关再开

double Now() {
    return gvnet::Socket_Now();
}

// 端点收到数据包时的回调：原样交给复制器处理
void OnMessage(void* user, uint32_t peerId, uint8_t type, const uint8_t* data, int len) {
    (void)user;
    g_rep.onMessage(g_ep, peerId, type, data, len);
}

// 端点层事件回调（对端加入/离开等）：同样转给复制器
void OnEvent(void* user, uint32_t peerId, int event) {
    (void)user;
    g_rep.onEvent(g_ep, peerId, event);
}

}

bool Gvtcier_Net_Init(uint16_t defaultPort) {
    // 重复初始化时先清理，避免旧的端点还在跑时被新配置覆盖
    if (g_inited) Gvtcier_Net_Shutdown();
    // 传 0 表示用默认端口，不允许端口被意外设成 0
    g_defaultPort = defaultPort ? defaultPort : gvnet::kDefaultPort;
    g_ep.setHandlers(OnMessage, OnEvent, nullptr);
    g_inited = true;
    return true;
}

void Gvtcier_Net_Shutdown() {
    g_ep.stop();
    g_rep.reset();
    g_inited = false;
}

bool Gvtcier_Net_StartServer(uint16_t port, const char* room, int maxPeers) {
    g_rep.reset();
    return g_ep.startServer(port ? port : g_defaultPort, room ? room : "", maxPeers);
}

bool Gvtcier_Net_StartClient(uint16_t port, const char* room) {
    g_rep.reset();
    return g_ep.startClient(port ? port : g_defaultPort, room ? room : "");
}

void Gvtcier_Net_Stop() {
    g_ep.stop();
    g_rep.reset();
}

bool Gvtcier_Net_ConnectDirect(const char* host, uint16_t port) {
    return g_ep.connectDirect(host ? host : "127.0.0.1", port ? port : g_defaultPort);
}

bool Gvtcier_Net_ConnectSignal(const char* host, uint16_t port, const char* room, bool forceRelay) {
    return g_ep.connectViaSignal(host ? host : "127.0.0.1", port ? port : g_defaultPort,
                                 room ? room : "", forceRelay);
}

bool Gvtcier_Net_RegisterRoom(const char* host, uint16_t port, bool forceRelay) {
    return g_ep.registerRoom(host ? host : "127.0.0.1", port ? port : g_defaultPort, forceRelay);
}

void Gvtcier_Net_Disconnect() {
    g_ep.disconnect();
}

int Gvtcier_Net_ScanRooms(char* names, int nameStride, int cap, uint16_t* ports) {
    if (!names || nameStride <= 0 || cap <= 0) return 0;
    std::vector<std::string> rn;
    std::vector<uint16_t> rp;
    int n = g_ep.scanRooms(rn, rp, cap);
    for (int i = 0; i < n; ++i) {
        char* dst = names + (size_t)i * (size_t)nameStride;
        memset(dst, 0, (size_t)nameStride);
        size_t len = rn[(size_t)i].size();
        if (len >= (size_t)nameStride) len = (size_t)nameStride - 1;
        memcpy(dst, rn[(size_t)i].data(), len);
        if (ports) ports[i] = rp[(size_t)i];
    }
    return n;
}

void Gvtcier_Net_SetScene(Scene* scene) {
    g_rep.setScene(scene);
}

void Gvtcier_Net_SetPlayer(Player* player) {
    g_rep.setLocalPlayer(player);
}

void Gvtcier_Net_SetRpcHandler(GvtcierNetRpcFn fn, void* user) {
    g_rep.setRpcHandler((gvnet::RpcFn)fn, user);
}

void Gvtcier_Net_SetSyncPlayer(bool on) {
    g_rep.setSyncPlayer(on);
}

void Gvtcier_Net_AddEntity(const char* name) {
    if (name) g_rep.addEntity(name);
}

void Gvtcier_Net_RemoveEntity(const char* name) {
    if (name) g_rep.removeEntity(name);
}

void Gvtcier_Net_ClearEntities() {
    g_rep.clearEntities();
}

void Gvtcier_Net_SendRpc(const char* name, const void* data, int len, uint32_t target) {
    if (name) g_rep.sendRpc(g_ep, name, data, len, target);
}

void Gvtcier_Net_Tick(float dt) {
    if (!g_inited || !g_ep.running()) return;
    double now = Now();
    g_rep.setNow(now);
    g_ep.tick(now);
    g_rep.tick(g_ep, now, dt);
}

uint16_t Gvtcier_Net_Port() {
    return g_ep.port();
}

uint32_t Gvtcier_Net_SelfId() {
    return g_ep.selfId();
}

int Gvtcier_Net_PeerCount() {
    return g_ep.peerCount();
}

int Gvtcier_Net_PlayerCount() {
    return (int)g_rep.players().size();
}

bool Gvtcier_Net_IsReady() {
    return g_ep.isReady();
}

bool Gvtcier_Net_IsServer() {
    return g_ep.role() == gvnet::Role::Server;
}

bool Gvtcier_Net_IsRelayed() {
    return g_ep.relayed();
}

float Gvtcier_Net_RttMs() {
    return g_ep.rttMs();
}

const char* Gvtcier_Net_Room() {
    return g_ep.roomName().c_str();
}
