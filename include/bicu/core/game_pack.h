#pragma once
#include <stdint.h>
#include <string>
#include "bicu/core/export.h"

#define GVTCIER_GAME_ABI 1u

#pragma pack(push, 8)
struct GvtcierGameInfo {
    uint32_t abi;
    const char* name;
    const char* description;
    const char* author;
    const char* gpak;
};
#pragma pack(pop)

typedef const GvtcierGameInfo* (*GvtcierGameInfoFn)();
typedef const char* (*GvtcierGameBytecodeFn)(uint64_t* size);
typedef const void* (*GvtcierGameResourceFn)(uint64_t* size);
typedef const void* (*GvtcierGameCoverFn)(uint64_t* size);

#define GVTCIER_GAME_INFO_SYMBOL "Gvtcier_Game_Info"
#define GVTCIER_GAME_BYTECODE_SYMBOL "Gvtcier_Game_Bytecode"
#define GVTCIER_GAME_RESOURCE_SYMBOL "Gvtcier_Game_Resource"
#define GVTCIER_GAME_COVER_SYMBOL "Gvtcier_Game_Cover"

struct GamePack {
    void* module = nullptr;
    GvtcierGameInfo info{};
    const char* bytecode = nullptr;
    uint64_t bytecodeSize = 0;
    const void* resource = nullptr;
    uint64_t resourceSize = 0;
    const void* cover = nullptr;
    uint64_t coverSize = 0;
    std::string pakPath;
    std::string name;
    std::string description;
    std::string author;
};

extern "C" {
GVTCIER_API bool Gvtcier_GamePack_Open(const char* dllPath, GamePack& out);
GVTCIER_API bool Gvtcier_GamePack_OpenGpk(const char* gpkPath, GamePack& out);
GVTCIER_API void Gvtcier_GamePack_Close(GamePack& pack);

GVTCIER_API int Gvtcier_Run(void* hostModule, const char* gameDllPath, const char* gameArgs);

GVTCIER_API int Gvtcier_CheckGvts(const char* gvtsPath, char* errOut, int errCap);
}
