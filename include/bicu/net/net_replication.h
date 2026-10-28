#pragma once
#include "bicu/core/export.h"
#include "bicu/net/net_peer.h"
#include "Gvtcier3D/scene/scene.h"
#include "Gvtcier3D/render/camera.h"
#include <string>
#include <vector>

namespace gvnet {

static const uint8_t MsgRepInput = 20;
static const uint8_t MsgRepRpc = 21;
static const uint8_t MsgRepSpawn = 22;
static const uint8_t MsgRepDespawn = 23;
static const uint8_t MsgRepPlayers = 24;

inline uint32_t NameHash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < s.size(); ++i) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

struct PlayerState {
    uint32_t peerId = 0;
    Vec3 pos;
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float velY = 0.0f;
    bool grounded = true;
    double lastInput = 0.0;
    Vec3 wishDir;
    bool jump = false;
};

struct SmoothObject {
    uint32_t hash = 0;
    Vec3 cur;
    Vec3 target;
    Vec3 curRot;
    Vec3 targetRot;
    bool visible = true;
    double lastSeen = 0.0;
};

struct BaselineField {
    uint32_t hash = 0;
    Vec3 pos;
    Vec3 rot;
    bool visible = true;
    bool valid = false;
};

typedef void (*RpcFn)(void* user, uint32_t peerId, const char* name, const void* data, int len);

class Replicator {
public:
    GVTCIER_API void reset();
    GVTCIER_API void setScene(Scene* s) { scene_ = s; }
    GVTCIER_API void setLocalPlayer(Player* p) { player_ = p; }
    GVTCIER_API void setRpcHandler(RpcFn fn, void* user) { rpcFn_ = fn; rpcUser_ = user; }
    GVTCIER_API void setSyncPlayer(bool on) { syncPlayer_ = on; }
    GVTCIER_API void setPrediction(bool on) { predictOn_ = on; }
    GVTCIER_API bool prediction() const { return predictOn_; }
    GVTCIER_API void setNow(double t) { now_ = t; }

    GVTCIER_API void addEntity(const std::string& name);
    GVTCIER_API void removeEntity(const std::string& name);
    GVTCIER_API void clearEntities() { entities_.clear(); announced_.clear(); }
    GVTCIER_API int entityCount() const { return (int)entities_.size(); }

    GVTCIER_API void sendRpc(Endpoint& ep, const char* name, const void* data, int len, uint32_t target);

    GVTCIER_API void onMessage(Endpoint& ep, uint32_t peerId, uint8_t type, const uint8_t* data, int len);
    GVTCIER_API void onEvent(Endpoint& ep, uint32_t peerId, int event);
    GVTCIER_API void tick(Endpoint& ep, double now, float dt);

    GVTCIER_API const std::vector<PlayerState>& players() const { return players_; }
    GVTCIER_API PlayerState* findPlayer(uint32_t peerId);

private:
    void serverTick(Endpoint& ep, double now, float dt);
    void clientTick(Endpoint& ep, double now, float dt);
    void sendSnapshot(Endpoint& ep);
    void sendPlayers(Endpoint& ep);
    void sendInput(Endpoint& ep, double now);
    void applySnapshot(const uint8_t* data, int len, double now);
    void applyPlayers(const uint8_t* data, int len, uint32_t selfId);
    void handleRpc(Endpoint& ep, uint32_t peerId, const uint8_t* data, int len);
    void handleSpawn(const uint8_t* data, int len);
    void handleDespawn(const uint8_t* data, int len);
    void handleInput(uint32_t peerId, const uint8_t* data, int len, double now);
    void simulate(PlayerState& st, float dt);
    Object* findObject(uint32_t hash);
    void smoothStep(float dt, double now);
    BaselineField* findBaseline(uint32_t hash);
    void resetBaseline();

    Scene* scene_ = nullptr;
    Player* player_ = nullptr;
    std::vector<std::string> entities_;
    std::vector<uint32_t> announced_;
    std::vector<uint32_t> pendingDespawn_;
    std::vector<uint32_t> freshPeers_;
    std::vector<PlayerState> players_;
    std::vector<SmoothObject> smooth_;
    std::vector<BaselineField> baseline_;
    bool forceFullSnapshot_ = true;
    bool syncPlayer_ = false;
    bool predictOn_ = true;
    double now_ = 0.0;
    double lastSnapshot_ = 0.0;
    double lastPlayers_ = 0.0;
    double lastInput_ = 0.0;
    RpcFn rpcFn_ = nullptr;
    void* rpcUser_ = nullptr;
};

}
