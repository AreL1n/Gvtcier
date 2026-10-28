#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {
//包格式魔数，按小端读出的字节是 'G''P''A''K'，用于快速识别文件类型。
const uint32_t GPAK_MAGIC = 0x4B415047;
//包格式版本号。字段布局变更时必须加一，旧引擎读到新版本会直接判为非法，避免解析错位。
const uint32_t GPAK_VERSION = 1;

//打开包文件，mode 为 "rb"/"wb"。路径按 UTF-8 处理并转宽字符，保证中文路径可用。
FILE* gpakOpen(const char* path, const char* mode) {
    if (!path || !mode) return NULL;
    std::wstring m(mode, mode + strlen(mode));
    return Gvtcier_File_Gfh(path, m.c_str());
}

//以下四个读写函数统一采用小端序，使包文件在不同架构上字节布局一致。
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

//打包时只允许这些扩展名入库。
//白名单而非黑名单是为了防止误把可执行文件、脚本源码之外的危险内容打进包。
bool endsWithAllowed(const char* path) {
    static const char* exts[] = { ".json", ".png", ".wav", ".obj", ".glb", ".txt", ".mp4" };
    size_t len = strlen(path);
    for (const char* e : exts) {
        size_t el = strlen(e);
        if (len >= el && strcasecmp(path + len - el, e) == 0) return true;
    }
    return false;
}
}

//标准 CRC32（多项式 0xEDB88320，反射实现）。
//查表在首次调用时构建并常驻，因此后续校验几乎零开销；用于检测包内数据损坏或被篡改。
uint32_t Gpak_Crc32(const void* data, uint64_t size) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    const unsigned char* p = (const unsigned char*)data;
    uint32_t c = 0xFFFFFFFFu;
    for (uint64_t i = 0; i < size; ++i)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t GpakReader::crc32(const unsigned char* data, uint64_t size) {
    return Gpak_Crc32(data, size);
}

//读取器统一入口：从磁盘整文件读入内存，然后解析目录。
//完全驻留内存是刻意的取舍：包通常只有几十 MB，换来随机访问不需要每次 seek，且字节快照的生命周期简单。
bool GpakReader::open(const char* path) {
    close();
    FILE* f = gpakOpen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 20) { fclose(f); return false; }
    raw_.resize((size_t)sz);
    if (fread(raw_.data(), 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; }
    fclose(f);
    return parse();
}

//直接从一段已有字节解析，不拷贝（raw_ 由 assign 复制一份）。
//用于游戏 DLL 内嵌资源包：引擎希望自己持有数据，避免复用模块内存。
bool GpakReader::openBytes(const void* data, uint64_t size) {
    close();
    //最小合法包头为 20 字节，小于它直接拒绝，避免后续越界读。
    if (!data || size < 20) return false;
    raw_.assign((const unsigned char*)data, (const unsigned char*)data + size);
    return parse();
}

//解析目录区。包的整体布局：
//  头部    魔数 u32 + 版本 u32 + 条目数 u32（共 12 字节）
//  条目表  每项：路径长度 u16 + 路径字节 + 数据偏移 u64 + 数据长度 u64 + CRC32 u32
//  数据区  各文件内容按条目顺序紧密排列
//每个字段读取前都检查剩余长度，任何越界或偏移超出文件大小都判为损坏，返回 false 而不是继续解析，
//这样坏包不会造成越界访问。
bool GpakReader::parse() {
    const unsigned char* p = raw_.data();
    if (getU32(p) != GPAK_MAGIC) return false;
    if (getU32(p + 4) != GPAK_VERSION) return false;
    uint32_t count = getU32(p + 8);
    size_t off = 12;
    entries_.clear();
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (off + 2 > raw_.size()) return false;
        uint16_t plen = (uint16_t)(p[off] | (p[off + 1] << 8));
        off += 2;
        if (off + plen + 20 > raw_.size()) return false;
        GpakEntry e;
        e.path.assign((const char*)p + off, plen);
        off += plen;
        e.offset = getU64(p + off); off += 8;
        e.size = getU64(p + off); off += 8;
        e.crc = getU32(p + off); off += 4;
        if (e.offset + e.size > (uint64_t)raw_.size()) return false;
        entries_.push_back(std::move(e));
    }
    return true;
}

//清空读取器状态，释放整包字节与条目表。
void GpakReader::close() {
    entries_.clear();
    raw_.clear();
}

//按条目路径线性查找是否存在。条目数通常几百，线性扫描比建哈希表更省内存，且只在加载期调用。
bool GpakReader::has(const char* storePath) const {
    for (const GpakEntry& e : entries_)
        if (e.path == storePath) return true;
    return false;
}

//解出一个条目到 out。先范围检查偏移，再拷贝数据，最后用 CRC 校验内容完整性。
//校验失败也返回 false，因此损坏的条目会像不存在一样被上层跳过，回落到更低的资源层。
bool GpakReader::extract(const char* storePath, std::vector<unsigned char>& out) const {
    for (const GpakEntry& e : entries_) {
        if (e.path != storePath) continue;
        if (e.offset + e.size > (uint64_t)raw_.size()) return false;
        out.assign(raw_.data() + e.offset, raw_.data() + e.offset + e.size);
        if (e.crc != Gpak_Crc32(out.data(), out.size())) return false;
        return true;
    }
    return false;
}

//解包并把内容写成磁盘文件，用于需要真实文件路径的场合（如交给外部工具处理）。返回是否写入成功。
bool GpakReader::extractTo(const char* storePath, const char* realPath) const {
    std::vector<unsigned char> out;
    if (!extract(storePath, out)) return false;
    FILE* f = gpakOpen(realPath, "wb");
    if (!f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

//打包：把磁盘上的若干文件按 storePaths 指定的包内路径写成一个 gpk。
//行文顺序：先写头部与条目表，此时数据偏移还不知道，先填 0；再追加数据区；
//最后回填每个条目的真实偏移（见下面的 pokeU64 循环）。这样只需一趟顺序写，不必多次 seek。
//realPaths 是源文件路径，storePaths 是包内路径，两者一一对应；count 为条目数。
//返回 false 表示扩展名不允许或文件读写失败。
bool Gpak_Build(const char* outGpk, const char* const* realPaths, const char* const* storePaths, int count) {
    GpakReader r;
    std::vector<unsigned char> buf;
    putU32(buf, GPAK_MAGIC);
    putU32(buf, GPAK_VERSION);
    putU32(buf, (uint32_t)count);
    std::vector<std::vector<unsigned char>> datas(count);
    for (int i = 0; i < count; ++i) {
        if (!endsWithAllowed(storePaths[i])) return false;
        FILE* f = gpakOpen(realPaths[i], "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        datas[i].resize((size_t)sz);
        bool ok = sz == 0 || fread(datas[i].data(), 1, (size_t)sz, f) == (size_t)sz;
        fclose(f);
        if (!ok) return false;
        uint16_t plen = (uint16_t)strlen(storePaths[i]);
        buf.push_back((unsigned char)(plen & 0xFF));
        buf.push_back((unsigned char)(plen >> 8));
        buf.insert(buf.end(), storePaths[i], storePaths[i] + plen);
        //偏移先占位写 0，等数据区写完后统一回填。
        putU64(buf, 0);
        putU64(buf, (uint64_t)datas[i].size());
        putU32(buf, Gpak_Crc32(datas[i].data(), datas[i].size()));
    }
    uint64_t dataOff = buf.size();
    for (int i = 0; i < count; ++i) {
        size_t tocPos = 12 + 0;
        (void)tocPos;
        buf.insert(buf.end(), datas[i].begin(), datas[i].end());
    }
    for (int i = 0; i < count; ++i) {
        //回填偏移：逐个累加前面条目头的实际大小（2 字节长度 + 路径 + 20 字节定长字段），
        //不能直接用固定步长，因为路径长度可变。
        size_t hdr = 12;
        for (int k = 0; k < i; ++k)
            hdr += 2 + strlen(storePaths[k]) + 20;
        size_t offPos = hdr + 2 + strlen(storePaths[i]) + 8;
        pokeU64(buf.data() + offPos, dataOff);
        dataOff += datas[i].size();
    }
    FILE* f = gpakOpen(outGpk, "wb");
    if (!f) return false;
    bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
    fclose(f);
    return ok;
}
