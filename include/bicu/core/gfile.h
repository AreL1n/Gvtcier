#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>

namespace gfile {

inline std::wstring toWide(const char* path) {
    if (!path || !path[0]) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (n < 1) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &w[0], n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

}

inline bool Gvtcier_File_Gexists(const char* path) {
    std::wstring w = gfile::toWide(path);
    if (w.empty()) return false;
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool Gvtcier_File_Gopen(const char* path, std::vector<unsigned char>& out) {
    out.clear();
    std::wstring w = gfile::toWide(path);
    if (w.empty()) return false;
    FILE* f = NULL;
    if (_wfopen_s(&f, w.c_str(), L"rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    bool ok = sz == 0 || fread(out.data(), 1, (size_t)sz, f) == (size_t)sz;
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

inline bool Gvtcier_File_Gsave(const char* path, const void* data, size_t size) {
    std::wstring w = gfile::toWide(path);
    if (w.empty()) return false;
    FILE* f = NULL;
    if (_wfopen_s(&f, w.c_str(), L"wb") != 0 || !f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

inline bool Gvtcier_File_Gappend(const char* path, const void* data, size_t size) {
    std::wstring w = gfile::toWide(path);
    if (w.empty()) return false;
    FILE* f = NULL;
    if (_wfopen_s(&f, w.c_str(), L"ab") != 0 || !f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

inline bool Gvtcier_File_GopenText(const char* path, std::string& out) {
    std::vector<unsigned char> raw;
    if (!Gvtcier_File_Gopen(path, raw)) { out.clear(); return false; }
    out.assign(raw.begin(), raw.end());
    return true;
}

inline bool Gvtcier_File_GsaveText(const char* path, const std::string& text) {
    return Gvtcier_File_Gsave(path, text.data(), text.size());
}

inline FILE* Gvtcier_File_Gfh(const char* path, const wchar_t* mode) {
    std::wstring w = gfile::toWide(path);
    if (w.empty()) return nullptr;
    FILE* f = nullptr;
    if (_wfopen_s(&f, w.c_str(), mode) != 0) return nullptr;
    return f;
}

#else

namespace gfile {

inline std::string toNarrow(const char* path) {
    return path ? std::string(path) : std::string();
}

inline std::string modeNarrow(const wchar_t* mode) {
    std::string m;
    if (mode) {
        for (const wchar_t* p = mode; *p; ++p) m.push_back((char)*p);
    }
    return m;
}

}

inline bool Gvtcier_File_Gexists(const char* path) {
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

inline bool Gvtcier_File_Gopen(const char* path, std::vector<unsigned char>& out) {
    out.clear();
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    bool ok = sz == 0 || fread(out.data(), 1, (size_t)sz, f) == (size_t)sz;
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

inline bool Gvtcier_File_Gsave(const char* path, const void* data, size_t size) {
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

inline bool Gvtcier_File_Gappend(const char* path, const void* data, size_t size) {
    if (!path || !path[0]) return false;
    FILE* f = fopen(path, "ab");
    if (!f) return false;
    bool ok = size == 0 || fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

inline bool Gvtcier_File_GopenText(const char* path, std::string& out) {
    std::vector<unsigned char> raw;
    if (!Gvtcier_File_Gopen(path, raw)) { out.clear(); return false; }
    out.assign(raw.begin(), raw.end());
    return true;
}

inline bool Gvtcier_File_GsaveText(const char* path, const std::string& text) {
    return Gvtcier_File_Gsave(path, text.data(), text.size());
}

inline FILE* Gvtcier_File_Gfh(const char* path, const wchar_t* mode) {
    if (!path || !path[0]) return nullptr;
    std::string m = gfile::modeNarrow(mode);
    return fopen(path, m.c_str());
}

#endif
