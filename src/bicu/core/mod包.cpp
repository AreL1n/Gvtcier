#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

namespace {
//模组包魔数，小端读出的字节为 'G''L''A''C'（Glc 的倒序写法）。
const uint32_t GLAC_MAGIC = 0x43414C47;
//模组包版本号，字段布局变化时递增。
const uint32_t GLAC_VERSION = 1;

//打开模组包文件，路径按 UTF-8 转宽字符，兼容中文目录。
FILE* glcOpen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    std::wstring m(mode, mode + strlen(mode));
    return Gvtcier_File_Gfh(path, m.c_str());
}

//小端序的整数读写辅助。
void putU32(std::vector<unsigned char>& b, uint32_t v) {
    b.push_back((unsigned char)(v & 0xFF));
    b.push_back((unsigned char)((v >> 8) & 0xFF));
    b.push_back((unsigned char)((v >> 16) & 0xFF));
    b.push_back((unsigned char)((v >> 24) & 0xFF));
}

void putU64(std::vector<unsigned char>& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back((unsigned char)((v >> (i * 8)) & 0xFF));
}

uint32_t getU32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t getU64(const unsigned char* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

void pokeU64(unsigned char* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)((v >> (i * 8)) & 0xFF);
}

//模组包允许的扩展名白名单。
//比主资源包多了 mp3、gvts、gmap、ghook：模组可以替换游戏脚本、地图与钩子，这正是模组的意义。
bool endsWithAllowed(const char* path) {
    static const char* exts[] = { ".json", ".png", ".wav", ".mp3", ".obj", ".glb", ".txt", ".mp4", ".gvts", ".gmap", ".ghook" };
    size_t len = strlen(path);
    for (const char* e : exts) {
        size_t el = strlen(e);
        if (len >= el && strcasecmp(path + len - el, e) == 0) return true;
    }
    return false;
}

//去掉字符串首尾的空白与换行。
std::string trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

//从一段 JSON 文本里抽取指定键的字符串值。
//这里不引入完整 JSON 解析器，因为元信息只有四个字段，代价不值得：
//做法是搜索 "键" 后找冒号，再要求值以双引号开头，逐字符拷贝并转义处理。
//值不是字符串（如数字、null）时返回空串。键名会被其他键的子串误命中的情况在元信息这种固定字段上可接受。
std::string findField(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return std::string();
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return std::string();
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size() || json[p] != '"') return std::string();
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            char c = json[p + 1];
            if (c == 'n') out.push_back('\n');
            else if (c == 't') out.push_back('\t');
            else out.push_back(c);
            p += 2;
            continue;
        }
        out.push_back(json[p]);
        ++p;
    }
    return out;
}

//解析模组元信息（名称、版本、作者、描述）。缺失字段留空，不影响模组加载。
void parseMeta(const std::string& json, GlcMeta& meta) {
    meta.name = findField(json, "name");
    meta.version = findField(json, "version");
    meta.author = findField(json, "author");
    meta.description = findField(json, "description");
}
}

//从磁盘打开模组包并解析。失败时清空内部字节，保持对象可继续复用。
bool GlcReader::open(const char* path) {
    close();
    FILE* f = glcOpen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 20) { fclose(f); return false; }
    raw_.resize((size_t)sz);
    if (fread(raw_.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); raw_.clear(); return false; }
    fclose(f);
    return parse();
}

//从内存字节解析模组包。size < 20 的情况被拒绝：最小结构为 12 字节头加至少 20 字节条目头。
bool GlcReader::openBytes(const void* data, uint64_t size) {
    close();
    if (!data || size < 20) return false;
    raw_.assign((const unsigned char*)data, (const unsigned char*)data + size);
    return parse();
}

//解析模组包。布局与 gpk 类似，差别在于头部多一段元信息：
//  魔数 u32 + 版本 u32 + 元信息长度 u32 + 元信息 JSON + 条目数 u32 + 条目表 + 数据区
//条目表每项同样为 路径长度 u16 + 路径 + 偏移 u64 + 长度 u64 + CRC32 u32。
//所有读写前都做边界检查，坏包只会解析失败，不会越界。
//成功后 valid_ 置真，之后 has/extract 才会工作。
bool GlcReader::parse() {
    const unsigned char* p = raw_.data();
    if (getU32(p) != GLAC_MAGIC) return false;
    if (getU32(p + 4) != GLAC_VERSION) return false;
    uint32_t metaLen = getU32(p + 8);
    size_t off = 12;
    if (off + metaLen + 4 > raw_.size()) return false;
    parseMeta(std::string((const char*)p + off, metaLen), meta_);
    off += metaLen;
    uint32_t count = getU32(p + off);
    off += 4;
    entries_.clear();
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (off + 2 > raw_.size()) return false;
        uint16_t plen = (uint16_t)(p[off] | (p[off + 1] << 8));
        off += 2;
        if (off + plen + 20 > raw_.size()) return false;
        GlcEntry e;
        e.path.assign((const char*)p + off, plen);
        off += plen;
        e.offset = getU64(p + off); off += 8;
        e.size = getU64(p + off); off += 8;
        e.crc = getU32(p + off); off += 4;
        if (e.offset + e.size > (uint64_t)raw_.size()) return false;
        entries_.push_back(std::move(e));
    }
    valid_ = true;
    return true;
}

//清空读取器：置无效、清元信息、清条目表、释放字节。
void GlcReader::close() {
    valid_ = false;
    meta_ = GlcMeta();
    entries_.clear();
    raw_.clear();
}

//查询模组内是否存在该路径。未成功解析的读取器一律返回 false。
bool GlcReader::has(const char* storePath) const {
    if (!valid_) return false;
    for (const GlcEntry& e : entries_)
        if (e.path == storePath) return true;
    return false;
}

//解出条目内容并用 CRC32 校验。校验失败视为未命中，让上层的资源查找继续往更低的层回落。
bool GlcReader::extract(const char* storePath, std::vector<unsigned char>& out) const {
    if (!valid_) return false;
    for (const GlcEntry& e : entries_) {
        if (e.path != storePath) continue;
        if (e.offset + e.size > (uint64_t)raw_.size()) return false;
        out.assign(raw_.data() + e.offset, raw_.data() + e.offset + e.size);
        if (e.crc != Gpak_Crc32(out.data(), out.size())) return false;
        return true;
    }
    return false;
}

//打包：把若干文件写成一个 glc 模组包。
//metaJson 为模组元信息 JSON；为空时写入空对象 "{}"，保证头部结构始终完整。
//流程与 Gpak_Build 相同：先写头与条目表（偏移留 0），再写数据区，最后回填偏移。
//一处差异是路径长度用 size_t 判断上限：超过 0xFFFF 就拒绝，因为包内长度字段只有 16 位。
//返回 false 表示扩展名不允许、路径过长或文件读写失败。
bool Glc_Build(const char* outGlc, const char* metaJson, const char* const* realPaths, const char* const* storePaths, int count) {
    if (!outGlc) return false;
    std::string meta = metaJson ? trim(std::string(metaJson)) : std::string();
    if (meta.empty()) meta = "{}";
    std::vector<unsigned char> buf;
    putU32(buf, GLAC_MAGIC);
    putU32(buf, GLAC_VERSION);
    putU32(buf, (uint32_t)meta.size());
    buf.insert(buf.end(), meta.begin(), meta.end());
    putU32(buf, (uint32_t)count);
    std::vector<std::vector<unsigned char>> datas(count);
    for (int i = 0; i < count; ++i) {
        if (!endsWithAllowed(storePaths[i])) return false;
        FILE* f = glcOpen(realPaths[i], "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        datas[i].resize((size_t)sz);
        bool ok = sz == 0 || fread(datas[i].data(), 1, (size_t)sz, f) == (size_t)sz;
        fclose(f);
        if (!ok) return false;
        size_t plen = strlen(storePaths[i]);
        if (plen > 0xFFFF) return false;
        buf.push_back((unsigned char)(plen & 0xFF));
        buf.push_back((unsigned char)((plen >> 8) & 0xFF));
        buf.insert(buf.end(), storePaths[i], storePaths[i] + plen);
        putU64(buf, 0);
        putU64(buf, (uint64_t)datas[i].size());
        putU32(buf, Gpak_Crc32(datas[i].data(), datas[i].size()));
    }
    uint64_t dataOff = buf.size();
    for (int i = 0; i < count; ++i) buf.insert(buf.end(), datas[i].begin(), datas[i].end());
    //条目表起点要跳过头部与元信息（12 + 元信息长度 + 条目数 4 字节），之后随路径长度逐步推进。
    size_t hdr = 12 + meta.size() + 4;
    for (int i = 0; i < count; ++i) {
        size_t offPos = hdr + 2 + strlen(storePaths[i]) + 8;
        pokeU64(buf.data() + offPos, dataOff);
        dataOff += datas[i].size();
        hdr += 2 + strlen(storePaths[i]) + 20;
    }
    FILE* f = glcOpen(outGlc, "wb");
    if (!f) return false;
    bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    return ok;
}
