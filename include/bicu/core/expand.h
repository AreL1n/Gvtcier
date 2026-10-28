#pragma once
#include "bicu/core/export.h"

class Scene;
class Player;
class PhysicsWorld;
class Camera;
class Window;
class Canvas2D;
class Renderer;

typedef void (*GvtcierExpandFn_Init)(void* ctx);
typedef void (*GvtcierExpandFn_Tick)(void* ctx, float dt);
typedef void (*GvtcierExpandFn_Draw)(void* ctx, void* canvas2d);
typedef void (*GvtcierExpandFn_Shutdown)(void* ctx);

#define GVTCIER_EXPAND_ABI 3

struct GvtcierExpandModule {
    void* handle = nullptr;
    char name[256] = "";
    GvtcierExpandFn_Init onInit = nullptr;
    GvtcierExpandFn_Tick onTick = nullptr;
    GvtcierExpandFn_Draw onDraw = nullptr;
    GvtcierExpandFn_Shutdown onShutdown = nullptr;
    int abiVersion = 0;
};

struct GvtcierExpandCtx {
    int abiVersion = 0;
    void* scene = nullptr;
    void* player = nullptr;
    void* physics = nullptr;
    void* camera = nullptr;
    void* window = nullptr;
    void* canvas2d = nullptr;
    void* renderer = nullptr;
    void* userData = nullptr;
    int windowWidth = 0;
    int windowHeight = 0;
};

extern "C" {
typedef int (*GvtcierExpandEntry)(GvtcierExpandCtx* ctx, GvtcierExpandModule* out);
}

GVTCIER_API void Gvtcier_Expand_LoadAll(GvtcierExpandCtx& ctx);
GVTCIER_API void Gvtcier_Expand_UnloadAll();
