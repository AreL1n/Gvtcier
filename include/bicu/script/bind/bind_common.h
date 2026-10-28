#pragma once
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "bicu/script/gvts_bind.h"
#include "bicu/core/GvtcierError.h"
#include "bicu/core/resource.h"
#include "bicu/core/math.h"
#ifndef GVWCIER_USE_GLES
#include <windows.h>
#endif
#include <gvts.h>
#include <Gauxlib.h>
#include <gvtslib.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <cmath>
#include <vector>
#ifndef GVWCIER_USE_GLES
#include "Gvtcier2D/interface/interface.h"
#include "bicu/audio/audio.h"
#endif
#include "Gvtcier3D/render/fx.h"
#include "Gvtcier3D/render/effect.h"
#include "Gvtcier3D/render/renderer.h"
#include "Gvtcier2D/render/canvas2d.h"
#include "Gvtcier3D/render/opacity.h"
#include "bicu/core/log.h"
#ifndef GVWCIER_USE_GLES
#include "bicu/core/win32_utf8.h"
#endif
#include "Rhidriver/rhi.h"
#include "Rhidriver/gl/gl1.h"

struct ObjHandle {
    GvtsEngineCtx* ctx;
    GvtId<Object> id;
    char name[128];
};

struct CapEntry {
    const char* name;
    const char* group;
    const char* desc;
    gvts_CFunction fn;
    std::string verb;
    std::string object;
};

Object* objFromHandle(ObjHandle* h);
Object* argObject(gvts_State* L, int idx);
void pushObjHandle(gvts_State* L, GvtsEngineCtx* ctx, GvtId<Object> id, const char* name);
int g_obj_index(gvts_State* L);
int g_obj_newindex(gvts_State* L);

void pushVec3(gvts_State* L, const Vec3& v);
void pushQuat(gvts_State* L, const Quat& q);
void pushMat4(gvts_State* L, const Mat4& m);
Vec3 readVec3(gvts_State* L, int idx);
Quat readQuat(gvts_State* L, int idx);
Mat4 readMat4(gvts_State* L, int idx);
int vec3Slots(gvts_State* L, int idx);

void regFunc(gvts_State* L, GvtsEngineCtx* ctx, const char* name, gvts_CFunction fn);
void regCap(gvts_State* L, GvtsEngineCtx* ctx, const char* group, const char* name,
            const char* desc, gvts_CFunction fn);
std::vector<CapEntry>& capRegistry();

struct ObjEntry {
    std::string name;
    std::string group;
};
std::vector<ObjEntry>& objRegistry();
void regObject(const char* name, const char* group);

int g_capFind(gvts_State* L);
int obj_playEffect(gvts_State* L);
int obj_stopEffect(gvts_State* L);
void gvtsResetAudioSlots();
bool processIncludes(std::string& code, bool& gvtEnabled, std::string& err);
bool gvtsFileIncluded(const char* name);
void applyGvtAttr(std::string& code);
void fixGameStartBlock(std::string& code);
void rewriteBlockHeaders(std::string& code);
bool checkScriptShape(const std::string& code, std::string& err);
void openMathLibCN(gvts_State* L);
void openMathGvLib(gvts_State* L);
void openOnlyGlLib(gvts_State* L, GvtsEngineCtx* ctx);
void registerOnlyGlCaps(gvts_State* L, GvtsEngineCtx* ctx);

void registerGvtApi(gvts_State* L, GvtsEngineCtx* ctx);
void openGameLibs(gvts_State* L);

void Gvtcier_Bind_Core(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Trigger_Tick(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Input(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Ui(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_UiControls(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Ui_Tick(GvtsEngineCtx* ctx);
void Gvtcier_Ui_End();
void Gvtcier_Bind_Trigger(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Scene(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_AnimSm(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Object(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Physics(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Player(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Camera(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Audio(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Render(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Canvas2d(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Texture(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Skeleton(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Effect(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Math(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Gl(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Common(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Time(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Bind_Net(gvts_State* L, GvtsEngineCtx* ctx);
void Gvtcier_Time_Tick(GvtsEngineCtx* ctx, float dt);
void Gvtcier_Time_Clear(GvtsEngineCtx* ctx);
bool Gvtcier_Prop_GetNum(Object* o, const char* key, int comp, double* out);
bool Gvtcier_Prop_SetNum(Object* o, const char* key, int comp, double v);
