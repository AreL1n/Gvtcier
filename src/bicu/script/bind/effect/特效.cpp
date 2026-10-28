#include "Gvtcier.h"

// 脚本绑定：特效。脚本播放/停止粒子特效，底层是 Gvtcier3D/render/特效.cpp。
// 特效资源按路径缓存，同一个路径只加载一次（GvtId<EffectAsset> 引用）。

// 按路径找已加载的特效资源，没有就加载。返回无效 ID 表示失败
static GvtId<EffectAsset> findOrLoadEffect(Renderer* renderer, const char* path) {
    if (!renderer) return GvtId<EffectAsset>{};
    return renderer->Gvtcier_Render_EffectAssetFor(path);
}

static int g_cr_playEffect(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* path = gvtsG_checkstring(L, 1);
    float x = (float)gvtsG_optnumber(L, 2, 0.0);
    float y = (float)gvtsG_optnumber(L, 3, 0.0);
    float z = (float)gvtsG_optnumber(L, 4, 0.0);
    bool loop = gvts_isnoneornil(L, 5) ? true : (gvts_toboolean(L, 5) != 0);
    if (!ctx->renderer) { gvts_pushnil(L); return 1; }
    GvtId<EffectAsset> asset = findOrLoadEffect(ctx->renderer, path);
    if (!asset.valid()) {
        printf(u8"[GvtcierGvts] 无法加载特效: %s\n", path);
        gvts_pushnil(L);
        return 1;
    }
    EffectSystem& sys = ctx->renderer->Gvtcier_Render_EffectSystem();
    GvtId<EffectInstance> handle = sys.Gvtcier_Effect_Play(asset, Vec3(x, y, z), loop);
    EffectInstance* inst = sys.instances().Gvtcier_Object_Get(handle);
    if (!inst) { gvts_pushnil(L); return 1; }
    gvts_pushinteger(L, (gvts_Integer)inst->id);
    return 1;
}

static int g_cr_stopEffect(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->renderer) { gvts_pushboolean(L, 0); return 1; }
    EffectSystem& sys = ctx->renderer->Gvtcier_Render_EffectSystem();
    if (gvts_isnumber(L, 1)) {
        gvts_pushboolean(L, sys.Gvtcier_Effect_StopById((uint64_t)gvts_tointeger(L, 1)) ? 1 : 0);
        return 1;
    }
    const char* name = gvtsG_checkstring(L, 1);
    sys.Gvtcier_Effect_Stop(name);
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_cr_isEffectPlaying(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->renderer) { gvts_pushboolean(L, 0); return 1; }
    EffectSystem& sys = ctx->renderer->Gvtcier_Render_EffectSystem();
    if (gvts_isnumber(L, 1)) {
        GvtId<EffectInstance> handle = sys.Gvtcier_Effect_FindById((uint64_t)gvts_tointeger(L, 1));
        EffectInstance* inst = sys.instances().Gvtcier_Object_Get(handle);
        gvts_pushboolean(L, inst && inst->playing);
        return 1;
    }
    const char* name = gvtsG_checkstring(L, 1);
    gvts_pushboolean(L, sys.Gvtcier_Effect_IsPlaying(name) ? 1 : 0);
    return 1;
}

static int g_cr_moveEffect(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->renderer) { gvts_pushboolean(L, 0); return 1; }
    uint64_t id = (uint64_t)gvtsG_checkinteger(L, 1);
    float x = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float z = (float)gvtsG_checknumber(L, 4);
    EffectSystem& sys = ctx->renderer->Gvtcier_Render_EffectSystem();
    gvts_pushboolean(L, sys.Gvtcier_Effect_SetPositionById(id, Vec3(x, y, z)) ? 1 : 0);
    return 1;
}

int obj_playEffect(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    const char* path = gvtsG_checkstring(L, 2);
    if (!op || !h->ctx->renderer) { gvts_pushboolean(L, 0); return 1; }
    GvtId<EffectAsset> asset = findOrLoadEffect(h->ctx->renderer, path);
    if (!asset.valid()) { gvts_pushboolean(L, 0); return 1; }
    EffectSystem& sys = h->ctx->renderer->Gvtcier_Render_EffectSystem();
    GvtId<EffectInstance> handle = sys.Gvtcier_Effect_Play(asset, Vec3(0, 0, 0), true, h->id);
    gvts_pushboolean(L, handle.valid() ? 1 : 0);
    return 1;
}

int obj_stopEffect(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op || !h->ctx->renderer) return 0;
    EffectSystem& sys = h->ctx->renderer->Gvtcier_Render_EffectSystem();
    GvtId<EffectInstance> handle = sys.Gvtcier_Effect_FindByObject(h->id);
    EffectInstance* inst = sys.instances().Gvtcier_Object_Get(handle);
    if (inst) inst->Gvtcier_Effect_Stop();
    return 0;
}

void Gvtcier_Bind_Effect(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"特效", u8"播放_特效", u8"在指定坐标播放特效，返回句柄；第五参数为是否循环", g_cr_playEffect);
    regCap(L, ctx, u8"特效", u8"停止_特效", u8"按句柄停止特效", g_cr_stopEffect);
    regCap(L, ctx, u8"特效", u8"检测_特效播放中", u8"查询特效句柄是否仍在播放", g_cr_isEffectPlaying);
    regCap(L, ctx, u8"特效", u8"移动_特效", u8"把特效移动到新的世界坐标", g_cr_moveEffect);
}
