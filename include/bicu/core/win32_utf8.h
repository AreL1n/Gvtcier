#pragma once
#include "bicu/core/export.h"
#include <string>
#include <vector>
#include <cstdint>

GVTCIER_API std::wstring Gvtcier_W32_ToWide(const char* utf8);
GVTCIER_API std::wstring Gvtcier_W32_ToWide(const std::string& utf8);
GVTCIER_API std::string Gvtcier_W32_ToUtf8(const wchar_t* wide);
GVTCIER_API std::string Gvtcier_W32_ToUtf8(const std::wstring& wide);

GVTCIER_API bool Gvtcier_W32_ToWideBuf(const char* utf8, wchar_t* out, int cap);
GVTCIER_API bool Gvtcier_W32_ToUtf8Buf(const wchar_t* wide, char* out, int cap);

GVTCIER_API bool Gvtcier_W32_IsUtf8CodePage();

GVTCIER_API std::string Gvtcier_W32_ModulePathUtf8();
GVTCIER_API std::string Gvtcier_W32_ModuleDirUtf8();
GVTCIER_API std::string Gvtcier_W32_FullPathUtf8(const char* utf8Path);
GVTCIER_API bool Gvtcier_W32_FileExistsUtf8(const char* utf8Path);

GVTCIER_API void* Gvtcier_W32_LoadLibraryUtf8(const char* utf8Path);
GVTCIER_API void* Gvtcier_W32_LoadLibraryExUtf8(const char* utf8Path, unsigned long flags);

GVTCIER_API std::vector<std::string> Gvtcier_W32_FindFilesUtf8(const char* utf8Pattern);

GVTCIER_API int Gvtcier_W32_MessageBoxUtf8(const char* utf8Text, const char* utf8Title, unsigned int flags);

GVTCIER_API bool Gvtcier_W32_SetWindowTextUtf8(void* hwnd, const char* utf8Text);
GVTCIER_API std::string Gvtcier_W32_GetWindowTextUtf8(void* hwnd);

GVTCIER_API std::string Gvtcier_W32_LastErrorUtf8(unsigned long code);
