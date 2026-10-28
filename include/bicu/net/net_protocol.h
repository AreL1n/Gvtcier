#pragma once
#include "bicu/core/export.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gvnet {

static const uint16_t kDefaultPort = 22800;
static const uint8_t kProtocolVersion = 1;
static const int kHeaderSize = 28;
static const int kMaxPacket = 1400;
static const int kMtuPayload = 1100;
static const int kFragChunk = kMtuPayload - 4;
static const double kFragTimeoutSec = 5.0;
static const size_t kFragMaxBytes = 1u << 20;

enum MsgType : uint8_t {
    MsgNone = 0,
    MsgHello = 1,
    MsgHelloAck = 2,
    MsgHeartbeat = 3,
    MsgHeartbeatAck = 4,
    MsgBye = 5,
    MsgReliable = 6,
    MsgSnapshot = 7,
    MsgAck = 8,
    MsgDiscover = 30,
    MsgDiscoverReply = 31,
    MsgSignalRegister = 40,
    MsgSignalQuery = 41,
    MsgSignalRoom = 42,
    MsgSignalPunch = 43,
    MsgSignalRelay = 44,
    MsgSignalUnregister = 45,
};

static const uint8_t kFlagReliable = 1;
static const uint8_t kFlagRelay = 2;

#pragma pack(push, 1)
struct PacketHeader {
    char magic[4];
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint8_t channel;
    uint32_t seq;
    uint32_t ack;
    uint32_t ackBits;
    uint32_t peerId;
    uint16_t payloadLen;
    uint16_t reserved;
};
#pragma pack(pop)

GVTCIER_API inline bool Header_IsValid(const PacketHeader& h) {
    return h.magic[0] == 'G' && h.magic[1] == 'V' && h.magic[2] == 'N' && h.magic[3] == 'T'
        && h.version == kProtocolVersion;
}

GVTCIER_API inline void Header_Write(const PacketHeader& h, uint8_t* out) {
    memcpy(out, "GVNT", 4);
    out[4] = h.version;
    out[5] = h.type;
    out[6] = h.flags;
    out[7] = h.channel;
    out[8] = (uint8_t)(h.seq & 0xFF);
    out[9] = (uint8_t)((h.seq >> 8) & 0xFF);
    out[10] = (uint8_t)((h.seq >> 16) & 0xFF);
    out[11] = (uint8_t)((h.seq >> 24) & 0xFF);
    out[12] = (uint8_t)(h.ack & 0xFF);
    out[13] = (uint8_t)((h.ack >> 8) & 0xFF);
    out[14] = (uint8_t)((h.ack >> 16) & 0xFF);
    out[15] = (uint8_t)((h.ack >> 24) & 0xFF);
    out[16] = (uint8_t)(h.ackBits & 0xFF);
    out[17] = (uint8_t)((h.ackBits >> 8) & 0xFF);
    out[18] = (uint8_t)((h.ackBits >> 16) & 0xFF);
    out[19] = (uint8_t)((h.ackBits >> 24) & 0xFF);
    out[20] = (uint8_t)(h.peerId & 0xFF);
    out[21] = (uint8_t)((h.peerId >> 8) & 0xFF);
    out[22] = (uint8_t)((h.peerId >> 16) & 0xFF);
    out[23] = (uint8_t)((h.peerId >> 24) & 0xFF);
    out[24] = (uint8_t)(h.payloadLen & 0xFF);
    out[25] = (uint8_t)((h.payloadLen >> 8) & 0xFF);
    out[26] = 0;
    out[27] = 0;
}

GVTCIER_API inline bool Header_Read(const uint8_t* in, int n, PacketHeader& h) {
    if (n < kHeaderSize) return false;
    memcpy(&h, in, kHeaderSize);
    return Header_IsValid(h);
}

GVTCIER_API inline bool Ack_Has(uint32_t ack, uint32_t bits, uint32_t seq) {
    if (seq == ack) return true;
    if (seq > ack) return false;
    uint32_t d = ack - seq;
    if (d > 32) return true;
    return (bits & (1u << (d - 1))) != 0;
}

class Writer {
public:
    std::vector<uint8_t> buf;
    GVTCIER_API void clear() { buf.clear(); }
    GVTCIER_API int size() const { return (int)buf.size(); }
    GVTCIER_API void u8(uint8_t v) { buf.push_back(v); }
    GVTCIER_API void u16(uint16_t v) {
        buf.push_back((uint8_t)(v & 0xFF));
        buf.push_back((uint8_t)((v >> 8) & 0xFF));
    }
    GVTCIER_API void u32(uint32_t v) {
        buf.push_back((uint8_t)(v & 0xFF));
        buf.push_back((uint8_t)((v >> 8) & 0xFF));
        buf.push_back((uint8_t)((v >> 16) & 0xFF));
        buf.push_back((uint8_t)((v >> 24) & 0xFF));
    }
    GVTCIER_API void i32(int32_t v) { u32((uint32_t)v); }
    GVTCIER_API void f32(float v) {
        uint32_t t;
        memcpy(&t, &v, 4);
        u32(t);
    }
    GVTCIER_API void vec3(const float* v) { f32(v[0]); f32(v[1]); f32(v[2]); }
    GVTCIER_API void vec3(const double* v) { f32((float)v[0]); f32((float)v[1]); f32((float)v[2]); }
    GVTCIER_API void bytes(const void* p, int n) {
        if (n <= 0) return;
        size_t old = buf.size();
        buf.resize(old + (size_t)n);
        memcpy(buf.data() + old, p, (size_t)n);
    }
    GVTCIER_API void str(const char* s) {
        if (!s) { u16(0); return; }
        size_t n = strlen(s);
        if (n > 4096) n = 4096;
        u16((uint16_t)n);
        bytes(s, (int)n);
    }
};

class Reader {
public:
    const uint8_t* p = nullptr;
    int n = 0;
    int off = 0;
    GVTCIER_API Reader(const void* d, int len) : p((const uint8_t*)d), n(len) {}
    GVTCIER_API bool ok() const { return p != nullptr && off <= n; }
    GVTCIER_API int remain() const { return off <= n ? n - off : 0; }
    GVTCIER_API uint8_t u8() {
        if (remain() < 1) { off = n + 1; return 0; }
        return p[off++];
    }
    GVTCIER_API uint16_t u16() {
        if (remain() < 2) { off = n + 1; return 0; }
        uint16_t v = (uint16_t)(p[off] | (p[off + 1] << 8));
        off += 2;
        return v;
    }
    GVTCIER_API uint32_t u32() {
        if (remain() < 4) { off = n + 1; return 0; }
        uint32_t v = (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8)
            | ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
        off += 4;
        return v;
    }
    GVTCIER_API int32_t i32() { return (int32_t)u32(); }
    GVTCIER_API float f32() {
        uint32_t t = u32();
        float v;
        memcpy(&v, &t, 4);
        return v;
    }
    GVTCIER_API void vec3(float* out) { out[0] = f32(); out[1] = f32(); out[2] = f32(); }
    GVTCIER_API void vec3(double* out) { out[0] = f32(); out[1] = f32(); out[2] = f32(); }
    GVTCIER_API void bytes(void* out, int len) {
        if (len <= 0) return;
        if (remain() < len) { off = n + 1; return; }
        memcpy(out, p + off, (size_t)len);
        off += len;
    }
    GVTCIER_API std::string str() {
        uint16_t len = u16();
        if (!ok() || remain() < (int)len) { off = n + 1; return std::string(); }
        std::string s((const char*)(p + off), len);
        off += len;
        return s;
    }
};

}
