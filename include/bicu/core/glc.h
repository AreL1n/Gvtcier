#pragma once
#include "bicu/core/export.h"
#include <string>
#include <vector>
#include <cstdint>

struct GlcEntry {
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t crc = 0;
};

struct GlcMeta {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
};

class GVTCIER_API GlcReader {
public:
    bool open(const char* path);
    bool openBytes(const void* data, uint64_t size);
    void close();
    bool valid() const { return valid_; }
    bool has(const char* storePath) const;
    bool extract(const char* storePath, std::vector<unsigned char>& out) const;
    const GlcMeta& meta() const { return meta_; }
    const std::vector<GlcEntry>& entries() const { return entries_; }

private:
    bool parse();
    bool valid_ = false;
    GlcMeta meta_;
    std::vector<GlcEntry> entries_;
    std::vector<unsigned char> raw_;
};

GVTCIER_API bool Glc_Build(const char* outGlc, const char* metaJson, const char* const* realPaths, const char* const* storePaths, int count);
