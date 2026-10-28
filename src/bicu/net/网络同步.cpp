#include "Gvtcier.h"
#include <cmath>
#include <cstring>

// 网络同步层：把场景对象的状态打包成快照发给对端，并在接收端还原。
// 两个通道：
//   实体快照  —— 生成/销毁/位置旋转缩放等变化，按 kSnapshotPerPacket 拆包
//   玩家状态  —— 高频的位置与朝向，单独一个通道，按 kPlayersPerPacket 拆包
// 接收端对位置做平滑插值（smooth_），避免网络抖动直接反映成画面抽搐。
// 关键原则：对象一律用 GvtId 引用，不用裸指针，因为网络层可能在任何时刻收到销毁消息。
namespace gvnet {

static const int kSnapshotPerPacket = 24;   // 单个实体快照包最多容纳多少条实体记录
static const int kPlayersPerPacket = 30;    // 单个玩家状态包最多容纳多少个玩家

// 把对象的生成信息写进包：名字、类型、位置、旋转、缩放、可见性、标签
static void WriteSpawnPayload(Writer& w, Object* o) {
    w.str(o->name.c_str());
    w.u8((uint8_t)o->kind);
    w.vec3(&o->pos.x);
    w.vec3(&o->rotDeg.x);
    w.vec3(&o->scale.x);
    w.u8(o->visible ? 1 : 0);
    w.str(o->tag.c_str());
}

void Replicator::reset() {
    entities_.clear();
    announced_.clear();
    pendingDespawn_.clear();
    freshPeers_.clear();
    players_.clear();
    smooth_.clear();
    baseline_.clear();
    forceFullSnapshot_ = true;
    lastSnapshot_ = 0.0;
    lastPlayers_ = 0.0;
    lastInput_ = 0.0;
}

Object* Replicator::findObject(uint32_t hash) {
    if (!scene_) return nullptr;
    Object* found = nullptr;
    scene_->ForEachObject([&](Object& o, GvtId<Object>) {
        if (found) return;
        if (NameHash(o.name) == hash) found = &o;
    });
    return found;
}

PlayerState* Replicator::findPlayer(uint32_t peerId) {
    for (size_t i = 0; i < players_.size(); ++i)
        if (players_[i].peerId == peerId) return &players_[i];
    return nullptr;
}

void Replicator::addEntity(const std::string& name) {
    if (name.empty()) return;
    for (size_t i = 0; i < entities_.size(); ++i)
        if (entities_[i] == name) return;
    entities_.push_back(name);
}

void Replicator::removeEntity(const std::string& name) {
    uint32_t hash = NameHash(name);
    for (size_t i = entities_.size(); i > 0; --i)
        if (entities_[i - 1] == name) entities_.erase(entities_.begin() + (long)(i - 1));
    for (size_t i = announced_.size(); i > 0; --i)
        if (announced_[i - 1] == hash) announced_.erase(announced_.begin() + (long)(i - 1));
    for (size_t i = baseline_.size(); i > 0; --i)
        if (baseline_[i - 1].hash == hash) baseline_.erase(baseline_.begin() + (long)(i - 1));
    pendingDespawn_.push_back(hash);
}

void Replicator::sendRpc(Endpoint& ep, const char* name, const void* data, int len, uint32_t target) {
    if (!name) return;
    Writer w;
    w.u32(target);
    w.str(name);
    if (len > 0 && data) w.bytes(data, len);
    if (ep.role() == Role::Client) {
        ep.sendReliable(1, MsgRepRpc, w.buf.data(), (int)w.buf.size());
        return;
    }
    std::vector<Link>& peers = ep.peers();
    for (size_t i = 0; i < peers.size(); ++i) {
        if (peers[i].state != LinkState::Ready) continue;
        if (target != 0 && target != peers[i].id) continue;
        ep.sendReliable(peers[i].id, MsgRepRpc, w.buf.data(), (int)w.buf.size());
    }
}

void Replicator::handleRpc(Endpoint& ep, uint32_t peerId, const uint8_t* data, int len) {
    Reader r(data, len);
    uint32_t target = r.u32();
    std::string name = r.str();
    if (!r.ok() || name.empty()) return;
    int off = r.off;
    int pay = len - off;
    if (pay < 0) pay = 0;
    bool deliver = (target == 0 || target == ep.selfId());
    if (ep.role() == Role::Server) {
        std::vector<Link>& peers = ep.peers();
        for (size_t i = 0; i < peers.size(); ++i) {
            if (peers[i].state != LinkState::Ready) continue;
            if (peers[i].id == peerId) continue;
            if (target != 0 && target != peers[i].id) continue;
            Writer w;
            w.u32(target);
            w.str(name.c_str());
            if (pay > 0) w.bytes(data + off, pay);
            ep.sendReliable(peers[i].id, MsgRepRpc, w.buf.data(), (int)w.buf.size());
        }
    }
    if (deliver && rpcFn_) rpcFn_(rpcUser_, peerId, name.c_str(), data + off, pay);
}

void Replicator::handleInput(uint32_t peerId, const uint8_t* data, int len, double now) {
    Reader r(data, len);
    float wx = r.f32();
    float wy = r.f32();
    float wz = r.f32();
    uint8_t jump = r.u8();
    float yaw = r.f32();
    float pitch = r.f32();
    if (!r.ok()) return;
    PlayerState* st = findPlayer(peerId);
    if (!st) {
        PlayerState ns;
        ns.peerId = peerId;
        ns.pos = Vec3(0, 2.0f, 0);
        players_.push_back(ns);
        st = &players_.back();
    }
    st->wishDir = Vec3(wx, wy, wz);
    st->jump = jump != 0;
    st->yawDeg = yaw;
    st->pitchDeg = pitch;
    st->lastInput = now;
}

void Replicator::simulate(PlayerState& st, float dt) {
    float g = scene_ ? scene_->gravity : 30.0f;
    if (g < 1.0f) g = 1.0f;
    float speed = (player_ && player_->speed > 0.01f) ? player_->speed : kPlayerWalkSpeed;
    st.pos = st.pos + st.wishDir * (speed * dt);
    if (st.jump && st.grounded) {
        st.velY = std::sqrt(2.0f * g * 1.5f);
        st.grounded = false;
        st.jump = false;
    }
    st.velY -= g * dt;
    st.pos.y += st.velY * dt;
    if (st.pos.y <= 0.0f) {
        st.pos.y = 0.0f;
        st.velY = 0.0f;
        st.grounded = true;
    }
}

void Replicator::applySnapshot(const uint8_t* data, int len, double now) {
    Reader r(data, len);
    uint8_t count = r.u8();
    for (int i = 0; i < count; ++i) {
        uint32_t hash = r.u32();
        uint8_t mask = r.u8();
        if (!r.ok()) return;
        SmoothObject* so = nullptr;
        for (size_t k = 0; k < smooth_.size(); ++k)
            if (smooth_[k].hash == hash) { so = &smooth_[k]; break; }
        if (!so) {
            SmoothObject ns;
            ns.hash = hash;
            smooth_.push_back(ns);
            so = &smooth_.back();
        }
        if (mask & 0x01) {
            float px = r.f32();
            float py = r.f32();
            float pz = r.f32();
            so->target = Vec3(px, py, pz);
            if (!so->lastSeen) so->cur = Vec3(px, py, pz);
        }
        if (mask & 0x02) {
            float rx = r.f32();
            float ry = r.f32();
            float rz = r.f32();
            so->targetRot = Vec3(rx, ry, rz);
            if (!so->lastSeen) so->curRot = Vec3(rx, ry, rz);
        }
        if (mask & 0x04) so->visible = r.u8() != 0;
        so->lastSeen = now;
    }
}

void Replicator::smoothStep(float dt, double now) {
    float k = dt * 12.0f;
    if (k > 1.0f) k = 1.0f;
    for (size_t i = smooth_.size(); i > 0; --i) {
        SmoothObject& so = smooth_[i - 1];
        Object* o = findObject(so.hash);
        if (!o || now - so.lastSeen > 3.0) {
            smooth_.erase(smooth_.begin() + (long)(i - 1));
            continue;
        }
        so.cur += (so.target - so.cur) * k;
        so.curRot += (so.targetRot - so.curRot) * k;
        o->pos = so.cur;
        o->rotDeg = so.curRot;
        o->visible = so.visible;
    }
}

void Replicator::applyPlayers(const uint8_t* data, int len, uint32_t selfId) {
    Reader r(data, len);
    uint8_t count = r.u8();
    for (int i = 0; i < count; ++i) {
        uint32_t peerId = r.u32();
        float px = r.f32();
        float py = r.f32();
        float pz = r.f32();
        float yaw = r.f32();
        float pitch = r.f32();
        if (!r.ok()) return;
        if (peerId == selfId) {
            if (player_) {
                if (predictOn_) {
                    Vec3 auth(px, py, pz);
                    Vec3 diff = auth - player_->pos;
                    if (diff.length() > 6.0f) {
                        player_->pos = auth;
                        player_->predictError = Vec3(0, 0, 0);
                    } else {
                        player_->predictError = diff;
                    }
                    if (syncPlayer_) {
                        float dyaw = yaw - player_->yawDeg;
                        while (dyaw > 180.0f) dyaw -= 360.0f;
                        while (dyaw < -180.0f) dyaw += 360.0f;
                        player_->predictErrorRot.y = dyaw;
                    }
                } else {
                    player_->pos = Vec3(px, py, pz);
                    if (syncPlayer_) {
                        player_->yawDeg = yaw;
                        player_->pitchDeg = pitch;
                    }
                }
            }
            continue;
        }
        std::string name = "Remote_" + std::to_string(peerId);
        Object* found = scene_ ? findObject(NameHash(name)) : nullptr;
        if (!scene_) continue;
        if (!found) {
            Object o;
            o.name = name;
            o.kind = ObjKind::Player;
            o.collider = ColliderType::None;
            o.pos = Vec3(px, py, pz);
            o.rotDeg = Vec3(pitch, yaw, 0);
            scene_->Gvtcier_Scene_RegisterObject(std::move(o));
            continue;
        }
        found->pos = Vec3(px, py, pz);
        found->rotDeg.y = yaw;
        found->rotDeg.x = pitch;
    }
}

void Replicator::handleSpawn(const uint8_t* data, int len) {
    Reader r(data, len);
    std::string name = r.str();
    uint8_t kind = r.u8();
    float px = r.f32();
    float py = r.f32();
    float pz = r.f32();
    float rx = r.f32();
    float ry = r.f32();
    float rz = r.f32();
    float sx = r.f32();
    float sy = r.f32();
    float sz = r.f32();
    uint8_t vis = r.u8();
    std::string tag = r.str();
    if (!r.ok() || name.empty() || !scene_) return;
    if (findObject(NameHash(name))) return;
    Object o;
    o.name = name;
    o.kind = (ObjKind)kind;
    o.pos = Vec3(px, py, pz);
    o.rotDeg = Vec3(rx, ry, rz);
    o.scale = Vec3(sx, sy, sz);
    o.visible = vis != 0;
    o.tag = tag;
    scene_->Gvtcier_Scene_RegisterObject(std::move(o));
}

void Replicator::handleDespawn(const uint8_t* data, int len) {
    Reader r(data, len);
    uint32_t hash = r.u32();
    if (!r.ok() || !scene_) return;
    GvtId<Object> victim;
    scene_->ForEachObject([&](const Object& o, GvtId<Object> id) {
        if (victim.valid()) return;
        if (NameHash(o.name) == hash) victim = id;
    });
    if (victim.valid()) scene_->queueRemove(victim);
    for (size_t i = smooth_.size(); i > 0; --i)
        if (smooth_[i - 1].hash == hash) smooth_.erase(smooth_.begin() + (long)(i - 1));
}

BaselineField* Replicator::findBaseline(uint32_t hash) {
    for (size_t i = 0; i < baseline_.size(); ++i)
        if (baseline_[i].hash == hash) return &baseline_[i];
    return nullptr;
}

void Replicator::resetBaseline() {
    baseline_.clear();
    forceFullSnapshot_ = true;
}

void Replicator::sendSnapshot(Endpoint& ep) {
    if (!scene_) return;
    std::vector<uint8_t> payload;
    int inPacket = 0;
    for (size_t i = 0; i < entities_.size(); ++i) {
        Object* o = findObject(NameHash(entities_[i]));
        if (!o) continue;
        if (inPacket >= kSnapshotPerPacket) {
            ep.broadcastUnreliable(MsgSnapshot, payload.data(), (int)payload.size());
            payload.clear();
            inPacket = 0;
        }
        if (payload.empty()) payload.push_back(0);
        uint32_t hash = NameHash(entities_[i]);
        BaselineField* base = findBaseline(hash);
        uint8_t mask = 0;
        bool fresh = forceFullSnapshot_ || !base || !base->valid;
        if (fresh) {
            mask = 0x07;
        } else {
            Vec3 dp = o->pos - base->pos;
            Vec3 dr = o->rotDeg - base->rot;
            if (std::fabs(dp.x) > 1e-4f || std::fabs(dp.y) > 1e-4f || std::fabs(dp.z) > 1e-4f)
                mask |= 0x01;
            if (std::fabs(dr.x) > 1e-3f || std::fabs(dr.y) > 1e-3f || std::fabs(dr.z) > 1e-3f)
                mask |= 0x02;
            if (o->visible != base->visible) mask |= 0x04;
        }
        if (!fresh && mask == 0) continue;
        if (!base) {
            BaselineField nb;
            nb.hash = hash;
            baseline_.push_back(nb);
            base = &baseline_.back();
        }
        base->pos = o->pos;
        base->rot = o->rotDeg;
        base->visible = o->visible;
        base->valid = true;
        Writer w;
        w.u32(hash);
        w.u8(mask);
        if (mask & 0x01) w.vec3(&o->pos.x);
        if (mask & 0x02) w.vec3(&o->rotDeg.x);
        if (mask & 0x04) w.u8(o->visible ? 1 : 0);
        payload.insert(payload.end(), w.buf.begin(), w.buf.end());
        inPacket++;
        payload[0] = (uint8_t)inPacket;
    }
    if (inPacket > 0) ep.broadcastUnreliable(MsgSnapshot, payload.data(), (int)payload.size());
    forceFullSnapshot_ = false;
}

void Replicator::sendPlayers(Endpoint& ep) {
    if (players_.empty()) return;
    std::vector<uint8_t> payload;
    int inPacket = 0;
    for (size_t i = 0; i < players_.size(); ++i) {
        if (inPacket >= kPlayersPerPacket) {
            ep.broadcastUnreliable(MsgRepPlayers, payload.data(), (int)payload.size());
            payload.clear();
            inPacket = 0;
        }
        if (payload.empty()) payload.push_back(0);
        Writer w;
        w.u32(players_[i].peerId);
        w.vec3(&players_[i].pos.x);
        w.f32(players_[i].yawDeg);
        w.f32(players_[i].pitchDeg);
        payload.insert(payload.end(), w.buf.begin(), w.buf.end());
        inPacket++;
        payload[0] = (uint8_t)inPacket;
    }
    if (inPacket > 0) ep.broadcastUnreliable(MsgRepPlayers, payload.data(), (int)payload.size());
}

void Replicator::sendInput(Endpoint& ep, double now) {
    if (!player_) return;
    Writer w;
    w.vec3(&player_->wishDir.x);
    w.u8(player_->jumpRequested ? 1 : 0);
    w.f32(player_->yawDeg);
    w.f32(player_->pitchDeg);
    ep.sendUnreliable(1, MsgRepInput, w.buf.data(), (int)w.buf.size());
    lastInput_ = now;
}

void Replicator::serverTick(Endpoint& ep, double now, float dt) {
    for (size_t i = 0; i < entities_.size(); ++i) {
        uint32_t hash = NameHash(entities_[i]);
        bool known = false;
        for (size_t k = 0; k < announced_.size(); ++k)
            if (announced_[k] == hash) { known = true; break; }
        if (known) continue;
        Object* o = findObject(hash);
        if (!o) continue;
        Writer w;
        WriteSpawnPayload(w, o);
        ep.broadcastReliable(MsgRepSpawn, w.buf.data(), (int)w.buf.size());
        announced_.push_back(hash);
    }
    for (size_t i = 0; i < freshPeers_.size(); ++i) {
        uint32_t pid = freshPeers_[i];
        for (size_t k = 0; k < entities_.size(); ++k) {
            Object* o = findObject(NameHash(entities_[k]));
            if (!o) continue;
            Writer w;
            WriteSpawnPayload(w, o);
            ep.sendReliable(pid, MsgRepSpawn, w.buf.data(), (int)w.buf.size());
        }
    }
    if (!freshPeers_.empty()) forceFullSnapshot_ = true;
    freshPeers_.clear();
    for (size_t i = 0; i < pendingDespawn_.size(); ++i) {
        Writer w;
        w.u32(pendingDespawn_[i]);
        ep.broadcastReliable(MsgRepDespawn, w.buf.data(), (int)w.buf.size());
    }
    pendingDespawn_.clear();

    if (player_) {
        PlayerState* self = findPlayer(1);
        if (!self) {
            PlayerState ns;
            ns.peerId = 1;
            players_.insert(players_.begin(), ns);
            self = &players_.front();
        }
        self->pos = player_->pos;
        self->yawDeg = player_->yawDeg;
        self->pitchDeg = player_->pitchDeg;
        self->lastInput = now;
    }
    for (size_t i = 0; i < players_.size(); ++i) {
        if (players_[i].peerId == 1) continue;
        simulate(players_[i], dt);
        if (players_[i].jump) players_[i].jump = false;
    }

    if (now - lastSnapshot_ >= 0.05) {
        sendSnapshot(ep);
        lastSnapshot_ = now;
    }
    if (now - lastPlayers_ >= 0.05) {
        sendPlayers(ep);
        lastPlayers_ = now;
    }
}

void Replicator::clientTick(Endpoint& ep, double now, float dt) {
    if (now - lastInput_ >= 1.0 / 30.0 && ep.isReady()) sendInput(ep, now);
    smoothStep(dt, now);
    if (predictOn_ && player_) {
        Vec3 err = player_->predictError;
        float el = err.length();
        if (el > 1e-5f) {
            float k = dt * 10.0f;
            if (k > 1.0f) k = 1.0f;
            Vec3 step = err * k;
            player_->pos = player_->pos + step;
            player_->predictError = err - step;
            if ((err - step).length() < 1e-4f) player_->predictError = Vec3(0, 0, 0);
        }
        float ey = player_->predictErrorRot.y;
        if (std::fabs(ey) > 1e-4f) {
            float k = dt * 10.0f;
            if (k > 1.0f) k = 1.0f;
            float step = ey * k;
            player_->yawDeg += step;
            player_->predictErrorRot.y = ey - step;
        }
    }
}

void Replicator::onMessage(Endpoint& ep, uint32_t peerId, uint8_t type, const uint8_t* data, int len) {
    switch (type) {
    case MsgSnapshot:
        if (ep.role() == Role::Client) applySnapshot(data, len, now_);
        break;
    case MsgRepPlayers:
        if (ep.role() == Role::Client) applyPlayers(data, len, ep.selfId());
        break;
    case MsgRepInput:
        if (ep.role() == Role::Server) handleInput(peerId, data, len, now_);
        break;
    case MsgRepRpc:
        handleRpc(ep, peerId, data, len);
        break;
    case MsgRepSpawn:
        if (ep.role() == Role::Client) handleSpawn(data, len);
        break;
    case MsgRepDespawn:
        if (ep.role() == Role::Client) handleDespawn(data, len);
        break;
    default:
        break;
    }
}

void Replicator::onEvent(Endpoint& ep, uint32_t peerId, int event) {
    if (ep.role() != Role::Server) return;
    if (event == kEventConnected) {
        if (peerId == 1) return;
        if (!findPlayer(peerId)) {
            PlayerState ns;
            ns.peerId = peerId;
            ns.pos = Vec3(0, 2.0f, 0);
            players_.push_back(ns);
        }
        freshPeers_.push_back(peerId);
    } else if (event == kEventDisconnected) {
        for (size_t i = players_.size(); i > 0; --i)
            if (players_[i - 1].peerId == peerId) players_.erase(players_.begin() + (long)(i - 1));
    }
}

void Replicator::tick(Endpoint& ep, double now, float dt) {
    if (!ep.running()) return;
    if (ep.role() == Role::Server) serverTick(ep, now, dt);
    else if (ep.role() == Role::Client) clientTick(ep, now, dt);
}

}
