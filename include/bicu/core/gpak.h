#pragma once
#include "bicu/core/export.h"
#include <string>
#include <vector>
#include <cstdint>

struct GpakEntry {
    std::string path;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint32_t crc = 0;
};

class GVTCIER_API GpakReader {
public:
    bool open(const char* path);
    bool openBytes(const void* data, uint64_t size);
    void close();
    bool has(const char* storePath) const;
    bool extract(const char* storePath, std::vector<unsigned char>& out) const;
    bool extractTo(const char* storePath, const char* realPath) const;
    const std::vector<GpakEntry>& entries() const { return entries_; }

private:
    static uint32_t crc32(const unsigned char* data, uint64_t size);
    bool parse();
    std::vector<GpakEntry> entries_;
    std::vector<unsigned char> raw_;
};

GVTCIER_API uint32_t Gpak_Crc32(const void* data, uint64_t size);
GVTCIER_API bool Gpak_Build(const char* outGpk, const char* const* realPaths, const char* const* storePaths, int count);
