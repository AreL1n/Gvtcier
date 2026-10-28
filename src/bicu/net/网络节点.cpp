#include "Gvtcier.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <algorithm>

// 网络节点层：在裸套接字之上实现房间、对等端管理、握手、心跳与超时。
// 它不关心游戏对象怎么同步（那是网络同步.cpp 的事），只负责"谁在线、包怎么组装和校验"。
// 所有发出去的包都带 PacketHeader，接收时先验头部再分发。
namespace gvnet {

static double Now() {
    return Socket_Now();
}

// 生成一个非零的随机密钥，用于握手阶段的会话标识。
// 种子混入当前时间与自身地址，避免多开进程时拿到同一串；用 xorshift 做低成本扩散。
static uint32_t RandKey() {
    static uint32_t s = 0;
    if (s == 0) {
        s = (uint32_t)(Now() * 1000000.0) ^ (uint32_t)(uintptr_t)&s;
        s = s ? s : 0x9E3779B9u;
    }
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s ? s : 1u;
}

static void BuildPacket(std::vector<uint8_t>& out, PacketHeader& h, const void* data, int len) {
    h.payloadLen = (uint16_t)(len > 0 ? len : 0);
    out.resize((size_t)kHeaderSize + (size_t)h.payloadLen);
    Header_Write(h, out.data());
    if (h.payloadLen > 0) memcpy(out.data() + kHeaderSize, data, h.payloadLen);
}

static void InitHeader(PacketHeader& h, uint32_t selfId, uint8_t type) {
    memcpy(h.magic, "GVNT", 4);
    h.version = kProtocolVersion;
    h.type = type;
    h.flags = 0;
    h.channel = 0;
    h.seq = 0;
    h.ack = 0;
    h.ackBits = 0;
    h.peerId = selfId;
    h.payloadLen = 0;
    h.reserved = 0;
}

static void MarkRecv(Link& lk, uint32_t seq) {
    if (seq == 0) return;
    if (seq > lk.recvSeq) {
        uint32_t d = seq - lk.recvSeq;
        if (d >= 32) { lk.recvBits = 0; }
        else { lk.recvBits = (lk.recvBits << d) | (1u << (d - 1)); }
        lk.recvSeq = seq;
    } else {
        uint32_t d = lk.recvSeq - seq;
        if (d >= 1 && d <= 32) lk.recvBits |= (1u << (d - 1));
    }
}

static void DropAcked(Link& lk, uint32_t ack, uint32_t bits) {
    if (ack == 0) return;
    for (size_t i = lk.outQueue.size(); i > 0; --i)
        if (Ack_Has(ack, bits, lk.outQueue[i - 1].seq))
            lk.outQueue.erase(lk.outQueue.begin() + (long)(i - 1));
}

bool Endpoint::startServer(uint16_t port, const std::string& room, int maxPeers) {
    stop();
    if (!Socket_Init()) return false;
    if (!sock_.open(port, true)) { Socket_Shutdown(); return false; }
    sock_.setBroadcast(true);
    role_ = Role::Server;
    selfId_ = 1;
    selfKey_ = RandKey();
    nextPeerId_ = 2;
    nextToken_ = 1;
    room_ = room;
    maxPeers_ = maxPeers > 0 ? maxPeers : 16;
    registered_ = false;
    relayOnly_ = false;
    hostToken_ = 0;
    selfToken_ = 0;
    directStart_ = 0.0;
    return true;
}

bool Endpoint::startClient(uint16_t port, const std::string& room) {
    stop();
    if (!Socket_Init()) return false;
    if (!sock_.open(port, false)) { Socket_Shutdown(); return false; }
    sock_.setBroadcast(true);
    role_ = Role::Client;
    selfId_ = 0;
    selfKey_ = RandKey();
    room_ = room;
    hostToken_ = 0;
    selfToken_ = 0;
    relayOnly_ = false;
    registered_ = false;
    signalAddr_ = Addr();
    server_ = Link();
    server_.id = 1;
    server_.key = selfKey_;
    return true;
}

bool Endpoint::running() const {
    return sock_.valid();
}

void Endpoint::stop() {
    bool had = sock_.valid();
    if (had && role_ == Role::Server) {
        for (size_t i = 0; i < peers_.size(); ++i) {
            if (peers_[i].state != LinkState::Ready) continue;
            PacketHeader h;
            InitHeader(h, selfId_, MsgBye);
            sendTo(peers_[i], h, nullptr, 0);
        }
    }
    peers_.clear();
    server_ = Link();
    rooms_.clear();
    tokens_.clear();
    role_ = Role::None;
    sock_.close();
    if (had) Socket_Shutdown();
}

void Endpoint::setHandlers(LinkMessageFn msg, LinkEventFn ev, void* user) {
    msgFn_ = msg;
    evFn_ = ev;
    user_ = user;
}

Link* Endpoint::linkById(uint32_t id) {
    if (role_ == Role::Client && id == 1) return &server_;
    for (size_t i = 0; i < peers_.size(); ++i)
        if (peers_[i].id == id) return &peers_[i];
    return nullptr;
}

Link* Endpoint::pbkey(uint32_t key) {
    if (key == 0) return nullptr;
    for (size_t i = 0; i < peers_.size(); ++i)
        if (peers_[i].key == key) return &peers_[i];
    return nullptr;
}

int Endpoint::peerCount() const {
    int c = 0;
    for (size_t i = 0; i < peers_.size(); ++i)
        if (peers_[i].state == LinkState::Ready) ++c;
    return c;
}

Link* Endpoint::findOrCreate(const Addr& from, uint32_t key) {
    if (key != 0) {
        Link* e = pbkey(key);
        if (e) {
            if (e->relayed && from.port != 0) {
                e->addr = from;
                e->relayed = false;
                e->via = Addr();
            }
            return e;
        }
    }
    for (size_t i = 0; i < peers_.size(); ++i) {
        if (peers_[i].relayed) continue;
        if (Socket::same(peers_[i].addr, from)) return &peers_[i];
    }
    if ((int)peers_.size() >= maxPeers_) return nullptr;
    Link lk;
    lk.id = nextPeerId_++;
    lk.key = key;
    lk.addr = from;
    lk.state = LinkState::Ready;
    lk.lastRecv = Now();
    peers_.push_back(lk);
    return &peers_.back();
}

void Endpoint::removeLink(uint32_t id, bool notify) {
    if (role_ == Role::Client) {
        if (id == 1 && server_.state == LinkState::Ready) {
            server_.state = LinkState::Handshaking;
            if (evFn_) evFn_(user_, 1, kEventDisconnected);
        }
        return;
    }
    for (size_t i = 0; i < peers_.size(); ++i) {
        if (peers_[i].id == id) {
            uint32_t pid = peers_[i].id;
            peers_.erase(peers_.begin() + (long)i);
            if (notify && evFn_) evFn_(user_, pid, kEventDisconnected);
            return;
        }
    }
}

void Endpoint::queueReliable(Link& lk, uint8_t type, const void* data, int len) {
    std::vector<uint8_t> logical;
    logical.reserve((size_t)len + 1);
    logical.push_back(type);
    if (len > 0 && data) logical.insert(logical.end(), (const uint8_t*)data, (const uint8_t*)data + len);
    int total = (int)logical.size();
    int chunks = (total + kFragChunk - 1) / kFragChunk;
    if (chunks < 1) chunks = 1;
    if (chunks > 255) chunks = 255;
    for (int i = 0; i < chunks; ++i) {
        ReliableOut o;
        o.seq = lk.sendRel++;
        int off = i * kFragChunk;
        int cn = total - off;
        if (cn > kFragChunk) cn = kFragChunk;
        if (cn < 0) cn = 0;
        o.payload.resize((size_t)cn + 2);
        o.payload[0] = (uint8_t)i;
        o.payload[1] = (uint8_t)chunks;
        if (cn > 0) memcpy(o.payload.data() + 2, logical.data() + off, (size_t)cn);
        o.lastSend = -1.0;
        o.tries = 0;
        lk.outQueue.push_back(o);
    }
}

void Endpoint::sendTo(Link& lk, const PacketHeader& h, const void* data, int len) {
    std::vector<uint8_t> pk;
    PacketHeader ph = h;
    BuildPacket(pk, ph, data, len);
    if (lk.relayed && lk.relayToken != 0 && signalAddr_.port != 0) {
        Writer w;
        w.u32(lk.relayToken);
        w.u32(selfToken_);
        w.bytes(pk.data(), (int)pk.size());
        PacketHeader rh;
        InitHeader(rh, selfId_, MsgSignalRelay);
        rh.flags = kFlagRelay;
        std::vector<uint8_t> rp;
        BuildPacket(rp, rh, w.buf.data(), (int)w.buf.size());
        sock_.sendTo(lk.via.port != 0 ? lk.via : signalAddr_, rp.data(), (int)rp.size());
        return;
    }
    sock_.sendTo(lk.addr, pk.data(), (int)pk.size());
}

void Endpoint::sendAck(Link& lk) {
    PacketHeader h;
    InitHeader(h, selfId_, MsgAck);
    h.ack = lk.recvSeq;
    h.ackBits = lk.recvBits;
    sendTo(lk, h, nullptr, 0);
}

void Endpoint::sendHello(Link& lk) {
    Writer w;
    w.u32(selfKey_);
    w.str(room_.c_str());
    PacketHeader h;
    InitHeader(h, selfId_, MsgHello);
    sendTo(lk, h, w.buf.data(), (int)w.buf.size());
    lk.helloSent = Now();
    lk.helloTries++;
}

void Endpoint::sendUnreliable(uint32_t id, uint8_t type, const void* data, int len) {
    Link* lk = linkById(id);
    if (!lk) return;
    PacketHeader h;
    InitHeader(h, selfId_, type);
    h.ack = lk->recvSeq;
    h.ackBits = lk->recvBits;
    sendTo(*lk, h, data, len);
}

void Endpoint::sendReliable(uint32_t id, uint8_t type, const void* data, int len) {
    Link* lk = linkById(id);
    if (!lk) return;
    queueReliable(*lk, type, data, len);
    flushQueue(*lk, Now());
}

void Endpoint::broadcastUnreliable(uint8_t type, const void* data, int len) {
    if (role_ == Role::Client) { sendUnreliable(1, type, data, len); return; }
    for (size_t i = 0; i < peers_.size(); ++i)
        if (peers_[i].state == LinkState::Ready) sendUnreliable(peers_[i].id, type, data, len);
}

void Endpoint::broadcastReliable(uint8_t type, const void* data, int len) {
    if (role_ == Role::Client) { sendReliable(1, type, data, len); return; }
    for (size_t i = 0; i < peers_.size(); ++i)
        if (peers_[i].state == LinkState::Ready) sendReliable(peers_[i].id, type, data, len);
}

void Endpoint::flushQueue(Link& lk, double now) {
    for (size_t i = 0; i < lk.outQueue.size(); ++i) {
        ReliableOut& o = lk.outQueue[i];
        if (o.tries >= 20) continue;
        if (o.lastSend >= 0.0 && now - o.lastSend < 0.15) continue;
        PacketHeader h;
        InitHeader(h, selfId_, MsgReliable);
        h.flags = kFlagReliable;
        h.seq = o.seq;
        h.ack = lk.recvSeq;
        h.ackBits = lk.recvBits;
        sendTo(lk, h, o.payload.data(), (int)o.payload.size());
        o.lastSend = now;
        o.tries++;
        lk.lastSend = now;
    }
}

bool Endpoint::connectDirect(const char* host, uint16_t port) {
    if (role_ != Role::Client) return false;
    Addr a = Socket::resolve(host, port);
    if (a.ip == 0) return false;
    server_ = Link();
    server_.id = 1;
    server_.key = selfKey_;
    server_.addr = a;
    server_.state = LinkState::Handshaking;
    directStart_ = Now();
    sendHello(server_);
    return true;
}

bool Endpoint::connectViaSignal(const char* signalHost, uint16_t signalPort, const std::string& room, bool forceRelay) {
    if (role_ != Role::Client) return false;
    signalAddr_ = Socket::resolve(signalHost, signalPort);
    if (signalAddr_.ip == 0) return false;
    relayOnly_ = forceRelay;
    if (!room.empty()) room_ = room;
    server_ = Link();
    server_.id = 1;
    server_.key = selfKey_;
    server_.state = LinkState::Handshaking;
    server_.addr = signalAddr_;
    directStart_ = Now();
    Writer w;
    w.str(room_.c_str());
    w.u8(forceRelay ? 1 : 0);
    sendSignal(MsgSignalQuery, w.buf.data(), (int)w.buf.size());
    return true;
}

bool Endpoint::registerRoom(const char* signalHost, uint16_t signalPort, bool forceRelay) {
    if (role_ != Role::Server) return false;
    signalAddr_ = Socket::resolve(signalHost, signalPort);
    if (signalAddr_.ip == 0) return false;
    relayOnly_ = forceRelay;
    registered_ = true;
    lastRegister_ = Now();
    Writer w;
    w.str(room_.c_str());
    sendSignal(MsgSignalRegister, w.buf.data(), (int)w.buf.size());
    return true;
}

void Endpoint::disconnect() {
    if (role_ == Role::Client && server_.state == LinkState::Ready) {
        PacketHeader h;
        InitHeader(h, selfId_, MsgBye);
        sendTo(server_, h, nullptr, 0);
        server_ = Link();
        server_.id = 1;
        server_.key = selfKey_;
        hostToken_ = 0;
    }
    if (role_ == Role::Server) {
        for (size_t i = 0; i < peers_.size(); ++i) {
            if (peers_[i].state != LinkState::Ready) continue;
            PacketHeader h;
            InitHeader(h, selfId_, MsgBye);
            sendTo(peers_[i], h, nullptr, 0);
        }
        if (registered_ && signalAddr_.port != 0) {
            Writer w;
            w.str(room_.c_str());
            sendSignal(MsgSignalUnregister, w.buf.data(), (int)w.buf.size());
        }
        peers_.clear();
    }
    registered_ = false;
}

void Endpoint::sendSignal(uint8_t type, const void* data, int len) {
    if (signalAddr_.ip == 0) return;
    PacketHeader h;
    InitHeader(h, selfId_, type);
    std::vector<uint8_t> pk;
    BuildPacket(pk, h, data, len);
    sock_.sendTo(signalAddr_, pk.data(), (int)pk.size());
}

void Endpoint::sendSignalTo(const Addr& to, uint8_t type, const void* data, int len) {
    if (to.ip == 0 || to.port == 0) return;
    PacketHeader h;
    InitHeader(h, selfId_, type);
    std::vector<uint8_t> pk;
    BuildPacket(pk, h, data, len);
    sock_.sendTo(to, pk.data(), (int)pk.size());
}

void Endpoint::tick(double now) {
    if (!sock_.valid()) return;
    pump(now);

    if (role_ == Role::Server) {
        if (registered_ && signalAddr_.port != 0 && now - lastRegister_ > 5.0) {
            Writer w;
            w.str(room_.c_str());
            sendSignal(MsgSignalRegister, w.buf.data(), (int)w.buf.size());
            lastRegister_ = now;
        }
        for (size_t i = 0; i < peers_.size(); ++i) {
            Link& lk = peers_[i];
            flushQueue(lk, now);
            if (lk.state == LinkState::Ready && now - lk.hbSent > 1.0) {
                sendUnreliable(lk.id, MsgHeartbeat, nullptr, 0);
                lk.hbSent = now;
            }
        }
        for (size_t i = peers_.size(); i > 0; --i)
            if (now - peers_[i - 1].lastRecv > 5.0) removeLink(peers_[i - 1].id, true);
        return;
    }

    if (role_ == Role::Client) {
        if (server_.state == LinkState::Handshaking) {
            if (hostToken_ != 0 && !relayOnly_ && !server_.relayed
                && now - directStart_ > 1.0 && signalAddr_.port != 0) {
                server_.relayed = true;
                server_.via = signalAddr_;
                server_.relayToken = hostToken_;
                server_.addr = signalAddr_;
                server_.helloSent = 0.0;
                sendHello(server_);
            }
            if (now - server_.helloSent > 0.25 && server_.helloTries < 40) sendHello(server_);
            if (now - directStart_ > 10.0) {
                server_.state = LinkState::Closing;
                if (evFn_) evFn_(user_, 1, kEventDisconnected);
            }
            return;
        }
        if (server_.state == LinkState::Ready) {
            flushQueue(server_, now);
            if (now - server_.hbSent > 1.0) {
                sendUnreliable(1, MsgHeartbeat, nullptr, 0);
                server_.hbSent = now;
            }
            if (now - server_.lastRecv > 5.0) removeLink(1, true);
        }
    }
}

void Endpoint::pump(double now) {
    uint8_t buf[kMaxPacket + 64];
    for (int i = 0; i < 128; ++i) {
        Addr from;
        int n = sock_.recvFrom(from, buf, (int)sizeof(buf));
        if (n <= 0) break;
        onPacket(from, buf, n, now);
    }
}

void Endpoint::deliverReliableChunk(Link& lk, const uint8_t* pl, int pn) {
    if (pn < 2) return;
    uint8_t fi = pl[0];
    uint8_t fc = pl[1];
    const uint8_t* chunk = pl + 2;
    int cn = pn - 2;
    if (fc <= 1) {
        if (cn >= 1) onLogical(lk, chunk[0], chunk + 1, cn - 1);
        return;
    }
    double now = Now();
    if (lk.inFrag && (now - lk.fragStart) > kFragTimeoutSec) {
        lk.inFrag = false;
        lk.fragAccum.clear();
        lk.fragHave = 0;
    }
    if (fi == 0) {
        lk.inFrag = true;
        lk.fragNeed = fc;
        lk.fragHave = 0;
        lk.fragStart = now;
        lk.fragAccum.assign((const char*)chunk, (size_t)cn);
    } else if (lk.inFrag && (int)fi == lk.fragHave) {
        lk.fragAccum.append((const char*)chunk, (size_t)cn);
    } else {
        return;
    }
    if (lk.fragAccum.size() > kFragMaxBytes) {
        lk.inFrag = false;
        lk.fragAccum.clear();
        lk.fragHave = 0;
        return;
    }
    lk.fragHave++;
    if (lk.inFrag && lk.fragHave >= lk.fragNeed) {
        lk.inFrag = false;
        if (!lk.fragAccum.empty())
            onLogical(lk, (uint8_t)lk.fragAccum[0],
                      (const uint8_t*)lk.fragAccum.data() + 1, (int)lk.fragAccum.size() - 1);
        lk.fragAccum.clear();
    }
}

void Endpoint::onLogical(Link& lk, uint8_t type, const uint8_t* data, int n) {
    if (msgFn_) msgFn_(user_, lk.id, type, data, n);
}

void Endpoint::processPacket(Link& lk, const uint8_t* data, int n, double now) {
    PacketHeader h;
    if (!Header_Read(data, n, h)) return;
    lk.lastRecv = now;
    const uint8_t* pl = data + kHeaderSize;
    int pn = n - kHeaderSize;
    if (pn < 0) pn = 0;
    if ((int)h.payloadLen < pn) pn = h.payloadLen;

    DropAcked(lk, h.ack, h.ackBits);

    switch (h.type) {
    case MsgHelloAck: {
        Reader r(pl, pn);
        selfId_ = r.u32();
        r.u32();
        if (lk.state != LinkState::Ready) {
            lk.state = LinkState::Ready;
            if (evFn_) evFn_(user_, lk.id, kEventConnected);
        }
        return;
    }
    case MsgHeartbeat: {
        PacketHeader ah;
        InitHeader(ah, selfId_, MsgHeartbeatAck);
        sendTo(lk, ah, nullptr, 0);
        return;
    }
    case MsgHeartbeatAck: {
        lk.rttMs = (float)((Now() - lk.hbSent) * 1000.0);
        return;
    }
    case MsgAck:
        return;
    case MsgBye:
        removeLink(lk.id, true);
        return;
    case MsgReliable: {
        uint32_t s = h.seq;
        MarkRecv(lk, s);
        if (s < lk.recvRel) { sendAck(lk); return; }
        if (s == lk.recvRel) {
            lk.recvRel++;
            deliverReliableChunk(lk, pl, pn);
            bool prog = true;
            while (prog) {
                prog = false;
                for (size_t i = 0; i < lk.pendingRel.size(); ++i) {
                    if (lk.pendingRel[i].first == lk.recvRel) {
                        const std::string& s2 = lk.pendingRel[i].second;
                        if (!s2.empty())
                            deliverReliableChunk(lk, (const uint8_t*)s2.data(), (int)s2.size());
                        lk.pendingRel.erase(lk.pendingRel.begin() + (long)i);
                        lk.recvRel++;
                        prog = true;
                        break;
                    }
                }
            }
        } else if (lk.pendingRel.size() < 256) {
            lk.pendingRel.push_back(std::make_pair(s, std::string((const char*)pl, (size_t)pn)));
        }
        sendAck(lk);
        return;
    }
    default:
        onLogical(lk, h.type, pl, pn);
        return;
    }
}

Link* Endpoint::handleHello(const Addr& from, uint32_t srcToken, const uint8_t* pl, int pn, double now) {
    Reader r(pl, pn);
    uint32_t key = r.u32();
    std::string room = r.str();
    if (!r.ok()) return nullptr;
    if (!room_.empty() && !room.empty() && room != room_) return nullptr;

    Link* lk = nullptr;
    bool isNew = false;
    if (srcToken != 0) {
        for (size_t i = 0; i < peers_.size(); ++i)
            if (peers_[i].relayed && peers_[i].relayToken == srcToken) { lk = &peers_[i]; break; }
        if (!lk) {
            if ((int)peers_.size() >= maxPeers_) return nullptr;
            Link nl;
            nl.id = nextPeerId_++;
            nl.key = key;
            nl.state = LinkState::Ready;
            nl.relayed = true;
            nl.relayToken = srcToken;
            nl.via = from;
            nl.lastRecv = now;
            peers_.push_back(nl);
            lk = &peers_.back();
            isNew = true;
        }
    } else {
        isNew = (pbkey(key) == nullptr);
        lk = findOrCreate(from, key);
        if (!lk) return nullptr;
    }
    lk->state = LinkState::Ready;
    lk->lastRecv = now;
    lk->helloPending = false;
    if (isNew && evFn_) evFn_(user_, lk->id, kEventConnected);

    Writer w;
    w.u32(lk->id);
    w.u32(selfKey_);
    PacketHeader h;
    InitHeader(h, selfId_, MsgHelloAck);
    sendTo(*lk, h, w.buf.data(), (int)w.buf.size());
    return lk;
}

void Endpoint::deliverRelayed(const Addr& from, uint32_t srcToken, const uint8_t* data, int n, double now) {
    PacketHeader h;
    if (!Header_Read(data, n, h)) return;
    const uint8_t* pl = data + kHeaderSize;
    int pn = n - kHeaderSize;
    if (pn < 0) pn = 0;
    if ((int)h.payloadLen < pn) pn = h.payloadLen;

    if (h.type == MsgHello) {
        handleHello(from, srcToken, pl, pn, now);
        return;
    }
    Link* lk = nullptr;
    if (role_ == Role::Server) {
        for (size_t i = 0; i < peers_.size(); ++i)
            if (peers_[i].relayed && peers_[i].relayToken == srcToken) { lk = &peers_[i]; break; }
    } else {
        if (server_.relayToken == srcToken || server_.relayToken == 0) lk = &server_;
        if (lk) {
            lk->relayed = true;
            if (lk->relayToken == 0) lk->relayToken = srcToken;
            if (lk->via.port == 0) lk->via = from;
        }
    }
    if (!lk) return;
    processPacket(*lk, data, n, now);
}

void Endpoint::handleSignal(const Addr& from, const PacketHeader& h, const uint8_t* data, int n, double now) {
    if (h.type == MsgSignalRegister) {
        Reader r(data, n);
        std::string room = r.str();
        if (room.empty()) return;
        RoomEntry* ex = nullptr;
        for (size_t i = 0; i < rooms_.size(); ++i)
            if (rooms_[i].room == room && Socket::same(rooms_[i].addr, from)) { ex = &rooms_[i]; break; }
        if (!ex) {
            for (size_t i = rooms_.size(); i > 0; --i)
                if (Socket::same(rooms_[i - 1].addr, from)) {
                    uint32_t dead = rooms_[i - 1].token;
                    for (size_t k = tokens_.size(); k > 0; --k)
                        if (tokens_[k - 1].first == dead) tokens_.erase(tokens_.begin() + (long)(k - 1));
                    rooms_.erase(rooms_.begin() + (long)(i - 1));
                }
            RoomEntry e;
            e.room = room;
            e.token = nextToken_++;
            e.addr = from;
            e.lastSeen = now;
            rooms_.push_back(e);
            tokens_.push_back(std::make_pair(e.token, from));
            ex = &rooms_.back();
        } else {
            ex->lastSeen = now;
        }
        Writer w;
        w.u8(1);
        w.u32(ex->token);
        w.u32(0);
        w.u32(0);
        w.u16(0);
        sendSignalTo(from, MsgSignalRoom, w.buf.data(), (int)w.buf.size());
        return;
    }
    if (h.type == MsgSignalQuery) {
        Reader r(data, n);
        std::string room = r.str();
        r.u8();
        RoomEntry* found = nullptr;
        for (size_t i = 0; i < rooms_.size(); ++i)
            if (rooms_[i].room == room) { found = &rooms_[i]; break; }
        Writer w;
        uint32_t ct = 0;
        if (!found) {
            w.u8(0);
            w.u32(0);
            w.u32(0);
            w.u32(0);
            w.u16(0);
        } else {
            ct = nextToken_++;
            tokens_.push_back(std::make_pair(ct, from));
            w.u8(1);
            w.u32(ct);
            w.u32(found->token);
            w.u32(found->addr.ip);
            w.u16(found->addr.port);
        }
        sendSignalTo(from, MsgSignalRoom, w.buf.data(), (int)w.buf.size());
        if (found) {
            Writer pw;
            pw.u32(ct);
            pw.u32(from.ip);
            pw.u16(from.port);
            PacketHeader ph;
            InitHeader(ph, selfId_, MsgSignalPunch);
            std::vector<uint8_t> pk;
            BuildPacket(pk, ph, pw.buf.data(), (int)pw.buf.size());
            sock_.sendTo(found->addr, pk.data(), (int)pk.size());
        }
        return;
    }
    if (h.type == MsgSignalUnregister) {
        for (size_t i = rooms_.size(); i > 0; --i)
            if (Socket::same(rooms_[i - 1].addr, from)) rooms_.erase(rooms_.begin() + (long)(i - 1));
        return;
    }
    if (h.type == MsgSignalRelay) {
        if (n < 8) return;
        Reader r(data, n);
        uint32_t dst = r.u32();
        uint32_t src = r.u32();
        const uint8_t* raw = data + 8;
        int rawLen = n - 8;
        if (rawLen <= 0) return;
        Addr target;
        bool have = false;
        if (role_ == Role::Server && selfToken_ != 0 && dst == selfToken_) {
            deliverRelayed(from, src, raw, rawLen, now);
            return;
        }
        if (role_ == Role::Server) {
            for (size_t i = 0; i < tokens_.size(); ++i)
                if (tokens_[i].first == dst) { target = tokens_[i].second; have = true; break; }
        }
        if (have) {
            Writer w;
            w.u32(dst);
            w.u32(src);
            w.bytes(raw, rawLen);
            PacketHeader rh;
            InitHeader(rh, selfId_, MsgSignalRelay);
            rh.flags = kFlagRelay;
            std::vector<uint8_t> pk;
            BuildPacket(pk, rh, w.buf.data(), (int)w.buf.size());
            sock_.sendTo(target, pk.data(), (int)pk.size());
        } else {
            deliverRelayed(from, src, raw, rawLen, now);
        }
        return;
    }
    if (h.type == MsgSignalRoom) {
        if (role_ == Role::None) return;
        Reader r(data, n);
        uint8_t ok = r.u8();
        uint32_t selfToken = r.u32();
        uint32_t peerToken = r.u32();
        uint32_t peerIp = r.u32();
        uint16_t peerPort = r.u16();
        if (!r.ok()) return;
        if (role_ == Role::Server) {
            if (ok) selfToken_ = selfToken;
            return;
        }
        selfToken_ = selfToken;
        if (!ok || peerToken == 0) return;
        hostToken_ = peerToken;
        if (relayOnly_) {
            server_.relayed = true;
            server_.via = signalAddr_;
            server_.relayToken = hostToken_;
            server_.addr = signalAddr_;
        } else {
            server_.addr = Socket::make(peerIp, peerPort);
        }
        server_.helloSent = 0.0;
        sendHello(server_);
        return;
    }
    if (h.type == MsgSignalPunch) {
        if (role_ != Role::Server) return;
        Reader r(data, n);
        uint32_t clientToken = r.u32();
        uint32_t ip = r.u32();
        uint16_t port = r.u16();
        if (!r.ok()) return;
        tokens_.push_back(std::make_pair(clientToken, Socket::make(ip, port)));
        PacketHeader ph;
        InitHeader(ph, selfId_, MsgHeartbeat);
        std::vector<uint8_t> pk;
        BuildPacket(pk, ph, nullptr, 0);
        sock_.sendTo(Socket::make(ip, port), pk.data(), (int)pk.size());
        return;
    }
}

void Endpoint::onPacket(const Addr& from, uint8_t* data, int n, double now) {
    PacketHeader h;
    if (!Header_Read(data, n, h)) return;
    const uint8_t* pl = data + kHeaderSize;
    int pn = n - kHeaderSize;
    if (pn < 0) pn = 0;
    if ((int)h.payloadLen < pn) pn = h.payloadLen;

    if (h.type == MsgDiscover) {
        if (role_ != Role::Server) return;
        Reader r(pl, pn);
        std::string room = r.str();
        if (!room_.empty() && !room.empty() && room != room_) return;
        Writer w;
        w.str(room_.c_str());
        w.u16(sock_.localPort());
        PacketHeader rh;
        InitHeader(rh, selfId_, MsgDiscoverReply);
        std::vector<uint8_t> pk;
        BuildPacket(pk, rh, w.buf.data(), (int)w.buf.size());
        sock_.sendTo(from, pk.data(), (int)pk.size());
        return;
    }
    if (h.type >= MsgSignalRegister && h.type <= MsgSignalUnregister) {
        handleSignal(from, h, pl, pn, now);
        return;
    }

    if (role_ == Role::Server) {
        if (h.type == MsgHello) { handleHello(from, 0, pl, pn, now); return; }
        for (size_t i = 0; i < peers_.size(); ++i) {
            if (!peers_[i].relayed && Socket::same(peers_[i].addr, from)) {
                processPacket(peers_[i], data, n, now);
                return;
            }
        }
        return;
    }

    if (role_ == Role::Client) {
        if (h.type == MsgHello) return;
        if (Socket::same(server_.addr, from) || server_.state != LinkState::Ready)
            processPacket(server_, data, n, now);
        return;
    }
}

int Endpoint::scanRooms(std::vector<std::string>& names, std::vector<uint16_t>& ports, int cap) {
    if (!running()) return 0;
    sock_.setBroadcast(true);
    Writer w;
    w.str(room_.c_str());
    PacketHeader h;
    InitHeader(h, selfId_, MsgDiscover);
    std::vector<uint8_t> pk;
    BuildPacket(pk, h, w.buf.data(), (int)w.buf.size());
    sock_.sendTo(Socket::make(0x7F000001u, kDefaultPort), pk.data(), (int)pk.size());
    sock_.sendTo(Socket::make(Socket::broadcastIp(), kDefaultPort), pk.data(), (int)pk.size());
    uint16_t lp = sock_.localPort();
    if (lp != kDefaultPort) sock_.sendTo(Socket::make(0x7F000001u, lp), pk.data(), (int)pk.size());

    int found = 0;
    double t0 = Now();
    while (Now() - t0 < 0.35 && found < cap) {
        Addr from;
        uint8_t buf[kMaxPacket];
        int n = sock_.recvFrom(from, buf, (int)sizeof(buf));
        if (n <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        PacketHeader rh;
        if (!Header_Read(buf, n, rh)) continue;
        if (rh.type != MsgDiscoverReply) continue;
        Reader r(buf + kHeaderSize, n - kHeaderSize);
        std::string nm = r.str();
        uint16_t pp = r.u16();
        if (!r.ok()) continue;
        bool dup = false;
        for (size_t i = 0; i < ports.size(); ++i)
            if (ports[i] == pp && names[i] == nm) { dup = true; break; }
        if (dup) continue;
        names.push_back(nm);
        ports.push_back(pp);
        found++;
    }
    return found;
}

}
