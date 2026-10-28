#include "Gvtcier.h"

// 脚本绑定：动画状态机。脚本定义状态、切换条件与过渡，引擎按帧推进。
// 每个状态机绑定一个蒙皮模型对象，状态里引用若干动画片段。

static GvtsEngineCtx* animCtx(gvts_State* L) {
    return (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
}

static AnimEase animEaseFromName(const char* s) {
    if (!s) return AnimEase::EaseInOut;
    if (strcmp(s, "线性") == 0) return AnimEase::Linear;
    if (strcmp(s, "缓入") == 0) return AnimEase::EaseIn;
    if (strcmp(s, "缓出") == 0) return AnimEase::EaseOut;
    if (strcmp(s, "缓入缓出") == 0) return AnimEase::EaseInOut;
    if (strcmp(s, "阶跃") == 0) return AnimEase::Step;
    return AnimEase::EaseInOut;
}

static int g_anim_create(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) { gvts_pushinteger(L, -1); return 1; }
    const char* name = gvtsG_checkstring(L, 1);
    Object* o = argObject(L, 2);
    GvtId<Object> tid = o ? ctx->scene->Gvtcier_Scene_IdOf(o) : GvtId<Object>{};
    int id = ctx->scene->Gvtcier_Scene_AnimSmCreate(name, tid);
    gvts_pushinteger(L, id);
    return 1;
}

static int g_anim_addState(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) return 0;
    int id = (int)gvtsG_checknumber(L, 1);
    const char* state = gvtsG_checkstring(L, 2);
    bool loop = gvts_toboolean(L, 3) != 0;
    float hold = (float)gvtsG_optnumber(L, 4, 0.0);
    ctx->scene->Gvtcier_Scene_AnimSmAddState(id, state, loop, hold);
    return 0;
}

static int g_anim_addTrack(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) return 0;
    int id = (int)gvtsG_checknumber(L, 1);
    const char* state = gvtsG_checkstring(L, 2);
    const char* key = gvtsG_checkstring(L, 3);
    int comp = (int)gvtsG_optnumber(L, 4, 0.0);
    double to = gvtsG_optnumber(L, 5, 0.0);
    float dur = (float)gvtsG_optnumber(L, 6, 0.3);
    const char* easeName = gvts_isstring(L, 7) ? gvts_tostring(L, 7) : nullptr;
    ctx->scene->Gvtcier_Scene_AnimSmAddTrack(id, state, key, comp, to, dur, animEaseFromName(easeName));
    return 0;
}

static int g_anim_set(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) { gvts_pushboolean(L, 0); return 1; }
    int id = (int)gvtsG_checknumber(L, 1);
    const char* state = gvtsG_checkstring(L, 2);
    bool immediate = gvts_toboolean(L, 3) != 0;
    bool ok = ctx->scene->Gvtcier_Scene_AnimSmSet(id, state, immediate);
    gvts_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int g_anim_setSpeed(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) return 0;
    int id = (int)gvtsG_checknumber(L, 1);
    float speed = (float)gvtsG_optnumber(L, 2, 1.0);
    ctx->scene->Gvtcier_Scene_AnimSmSetSpeed(id, speed);
    return 0;
}

static int g_anim_current(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) { gvts_pushstring(L, ""); return 1; }
    int id = (int)gvtsG_checknumber(L, 1);
    gvts_pushstring(L, ctx->scene->Gvtcier_Scene_AnimSmCurrent(id));
    return 1;
}

static int g_anim_find(gvts_State* L) {
    GvtsEngineCtx* ctx = animCtx(L);
    if (!ctx->scene) { gvts_pushinteger(L, -1); return 1; }
    const char* name = gvtsG_checkstring(L, 1);
    gvts_pushinteger(L, ctx->scene->Gvtcier_Scene_AnimSmFind(name));
    return 1;
}

void Gvtcier_Bind_AnimSm(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"动画", u8"新建_动画状态机", u8"创建一个绑定到对象的动画状态机，返回编号：新建_动画状态机(\"名\", 对象)", g_anim_create);
    regCap(L, ctx, u8"动画", u8"添加_动画状态", u8"为状态机登记一个状态：添加_动画状态(编号, \"状态名\", 是否循环, 停留秒)", g_anim_addState);
    regCap(L, ctx, u8"动画", u8"添加_动画轨道", u8"给状态加一条属性轨道：添加_动画轨道(编号, \"状态名\", \"属性\", 分量, 目标值, 时长, \"缓动\")", g_anim_addTrack);
    regCap(L, ctx, u8"动画", u8"切换_动画状态", u8"切换到某状态，第三参数为真则立即到位：切换_动画状态(编号, \"状态名\", 立即)", g_anim_set);
    regCap(L, ctx, u8"动画", u8"设置_动画速度", u8"设置状态机播放倍速：设置_动画速度(编号, 速度)", g_anim_setSpeed);
    regCap(L, ctx, u8"动画", u8"获取_动画状态", u8"取当前状态名：获取_动画状态(编号)", g_anim_current);
    regCap(L, ctx, u8"动画", u8"查找_动画状态机", u8"按名称查编号，找不到返回 -1：查找_动画状态机(\"名\")", g_anim_find);
}
