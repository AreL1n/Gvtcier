#pragma once
#include "bicu/core/export.h"
#include "bicu/core/glc.h"
#include <vector>
#include <string>
#include <cstdint>

GVTCIER_API void Gvtcier_Res_SetPack(const void* data, uint64_t size);
GVTCIER_API void Gvtcier_Res_SetPackFile(const char* path);
GVTCIER_API bool Gvtcier_Res_SetPackFileOk(const char* path);
GVTCIER_API void Gvtcier_Res_ClearPack();
GVTCIER_API bool Gvtcier_Res_HasPack();
GVTCIER_API bool Gvtcier_Res_Read(const char* logicalPath, std::vector<unsigned char>& out);
GVTCIER_API bool Gvtcier_Res_Exists(const char* logicalPath);
GVTCIER_API bool Gvtcier_Res_ReadFile(const char* assetPath, std::vector<unsigned char>& out);

GVTCIER_API bool Gvtcier_Res_AddMod(const char* glcPath);
GVTCIER_API void Gvtcier_Res_ClearMods();
GVTCIER_API int Gvtcier_Res_ModCount();
GVTCIER_API const GlcMeta* Gvtcier_Res_ModMeta(int index);
GVTCIER_API int Gvtcier_Res_ListMod(const char* prefix, std::vector<std::string>& out);
GVTCIER_API bool Gvtcier_Res_ReadMain(const char* assetPath, std::vector<unsigned char>& out);
