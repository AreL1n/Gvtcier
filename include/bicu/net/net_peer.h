#pragma once
#include "bicu/core/export.h"
#include "bicu/net/net_socket.h"
#include "bicu/net/net_protocol.h"
#include <string>
#include <vector>

namespace gvnet {

enum class Role { None, Server, Client };

enum class LinkState { Handshaking, Ready, Closing };

struct ReliableOut {
    uint32_t seq = 0;
    std::vector<uint8_t> payload;
    double lastSend = 0.0;
    int tries = 0;
};

struct Link {
    uint32_t id = 0;
    uint32_t key = 0;
    uint32_t relayToken = 0;
    Addr addr;
    Addr via;
    bool relayed = false;
    LinkState state = LinkState::Handshaking;
    double lastRecv = 0.0;
    double lastSend = 0.0;
    float rttMs = 0.0f;
    uint32_t sendSeq = 0;
    uint32_t recvSeq = 0;
    uint32_t recvBits = 0;
    uint32_t recvRel = 1;
    uint32_t sendRel = 1;
    int helloTries = 0;
    double hbSent = 0.0;
    double helloSent = 0.0;
    bool helloPending = false;
    std::vector<ReliableOut> outQueue;
    std::vector<std::pair<uint32_t, std::string>> pendingRel;
    std::string fragAccum;
    int fragNeed = 0;
    int fragHave = 0;
    bool inFrag = false;
    double fragStart = 0.0;
    std::string playerName;
};

static const int kEventConnected = 1;
static const int kEventDisconnected = 2;

struct RoomEntry {
    std::string room;
    uint32_t token = 0;
    Addr addr;
    double lastSeen = 0.0;
};

typedef void (*LinkMessageFn)(void* user, uint32_t peerId, uint8_t type, const uint8_t* data, int len);
typedef void (*LinkEventFn)(void* user, uint32_t peerId, int event);

class GVTCIER_API Endpoint {
public:
    GVTCIER_API bool startServer(uint16_t port, const std::string& room, int maxPeers);
    GVTCIER_API bool startClient(uint16_t port, const std::string& room);
    GVTCIER_API void stop();
    GVTCIER_API bool running() const;
    GVTCIER_API Role role() const { return role_; }
    GVTCIER_API uint16_t port() const { return sock_.localPort(); }

    GVTCIER_API void setHandlers(LinkMessageFn msg, LinkEventFn ev, void* user);

    GVTCIER_API bool connectDirect(const char* host, uint16_t port);
    GVTCIER_API bool connectViaSignal(const char* signalHost, uint16_t signalPort, const std::string& room, bool forceRelay);
    GVTCIER_API bool registerRoom(const char* signalHost, uint16_t signalPort, bool forceRelay);
    GVTCIER_API void disconnect();

    GVTCIER_API Link* linkById(uint32_t id);
    GVTCIER_API Link* serverLink() { return role_ == Role::Client ? &server_ : nullptr; }
    GVTCIER_API std::vector<Link>& peers() { return peers_; }
    GVTCIER_API int peerCount() const;

    GVTCIER_API void sendReliable(uint32_t id, uint8_t type, const void* data, int len);
    GVTCIER_API void sendUnreliable(uint32_t id, uint8_t type, const void* data, int len);
    GVTCIER_API void broadcastReliable(uint8_t type, const void* data, int len);
    GVTCIER_API void broadcastUnreliable(uint8_t type, const void* data, int len);

    GVTCIER_API int scanRooms(std::vector<std::string>& names, std::vector<uint16_t>& ports, int cap);
    GVTCIER_API void tick(double now);

    GVTCIER_API uint32_t selfId() const { return selfId_; }
    GVTCIER_API uint32_t selfKey() const { return selfKey_; }
    GVTCIER_API float rttMs() const { return server_.rttMs; }
    GVTCIER_API bool isReady() const { return role_ == Role::Client && server_.state == LinkState::Ready; }
    GVTCIER_API const std::string& roomName() const { return room_; }
    GVTCIER_API bool relayed() const { return server_.relayed; }

private:
    void pump(double now);
    void onPacket(const Addr& from, uint8_t* data, int n, double now);
    void processPacket(Link& lk, const uint8_t* data, int n, double now);
    void onLogical(Link& lk, uint8_t type, const uint8_t* data, int n);
    void deliverReliableChunk(Link& lk, const uint8_t* pl, int pn);
    Link* handleHello(const Addr& from, uint32_t srcToken, const uint8_t* pl, int pn, double now);
    void sendHello(Link& lk);
    void sendAck(Link& lk);
    Link* findOrCreate(const Addr& from, uint32_t key);
    void queueReliable(Link& lk, uint8_t type, const void* data, int len);
    void removeLink(uint32_t id, bool notify);
    void flushQueue(Link& lk, double now);
    void sendTo(Link& lk, const PacketHeader& h, const void* data, int len);
    Link* pbkey(uint32_t key);
    void handleSignal(const Addr& from, const PacketHeader& h, const uint8_t* data, int n, double now);
    void deliverRelayed(const Addr& from, uint32_t srcToken, const uint8_t* data, int n, double now);
    void sendSignal(uint8_t type, const void* data, int len);
    void sendSignalTo(const Addr& to, uint8_t type, const void* data, int len);

    Socket sock_;
    Role role_ = Role::None;
    uint32_t selfId_ = 0;
    uint32_t selfKey_ = 0;
    uint32_t nextPeerId_ = 2;
    uint32_t nextToken_ = 1;
    int maxPeers_ = 16;
    Link server_;
    std::vector<Link> peers_;
    std::string room_;
    Addr signalAddr_;
    bool relayOnly_ = false;
    bool registered_ = false;
    double lastHeartbeat_ = 0.0;
    double lastRegister_ = 0.0;
    double directStart_ = 0.0;
    double scanUntil_ = 0.0;
    uint32_t hostToken_ = 0;
    uint32_t selfToken_ = 0;
    std::vector<RoomEntry> rooms_;
    std::vector<std::pair<uint32_t, Addr>> tokens_;
    LinkMessageFn msgFn_ = nullptr;
    LinkEventFn evFn_ = nullptr;
    void* user_ = nullptr;
};

}
