#include "Gvtcier.h"

// 脚本绑定：物理。脚本查询玩家是否着地、做射线检测、施加速度等。
// 底层是 Gvtcier3D/physics/物理.cpp，这里只做参数转换与结果打包。

// 玩家当前是否站在地面上（用于判断能否跳跃）
static int g_cr_isGrounded(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->phys) { gvts_pushboolean(L, ctx->phys->Gvtcier_Physics_PlayerGrounded()); return 1; }
    gvts_pushboolean(L, 0);
    return 1;
}

static int g_cr_onPlatform(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->phys) { gvts_pushboolean(L, ctx->phys->Gvtcier_Physics_PlayerOnPlatform()); return 1; }
    gvts_pushboolean(L, 0);
    return 1;
}

static int g_cr_castRayHit(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float ox = (float)gvtsG_checknumber(L, 1);
    float oy = (float)gvtsG_checknumber(L, 2);
    float oz = (float)gvtsG_checknumber(L, 3);
    float dx = (float)gvtsG_checknumber(L, 4);
    float dy = (float)gvtsG_checknumber(L, 5);
    float dz = (float)gvtsG_checknumber(L, 6);
    float len = (float)gvtsG_optnumber(L, 7, 10.0);
    if (!ctx->phys || !ctx->scene) { gvts_pushnil(L); return 1; }
    Vec3 pt, nrm;
    float dist = -1.0f;
    GvtId<Object> hit = ctx->phys->Gvtcier_Physics_CastRayEx(*ctx->scene, Vec3(ox, oy, oz), Vec3(dx, dy, dz), len,
                                                   &pt, &nrm, &dist);
    const Object* ho = ctx->scene->Gvtcier_Scene_GetObject(hit);
    if (!ho) { gvts_pushnil(L); return 1; }

    const char* rn = ho->name.c_str();
    if (!rn || !rn[0]) rn = ho->uid.c_str();
    if (!rn || !rn[0]) rn = "命中体";

    gvts_pushstring(L, rn);
    gvts_pushnumber(L, pt.x);
    gvts_pushnumber(L, pt.y);
    gvts_pushnumber(L, pt.z);
    gvts_pushnumber(L, nrm.x);
    gvts_pushnumber(L, nrm.y);
    gvts_pushnumber(L, nrm.z);
    gvts_pushnumber(L, dist);
    gvts_pushstring(L, ho->uid.c_str());
    return 9;
}

static int g_cr_castRaySurface(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float ox = (float)gvtsG_checknumber(L, 1);
    float oy = (float)gvtsG_checknumber(L, 2);
    float oz = (float)gvtsG_checknumber(L, 3);
    float dx = (float)gvtsG_checknumber(L, 4);
    float dy = (float)gvtsG_checknumber(L, 5);
    float dz = (float)gvtsG_checknumber(L, 6);
    float len = (float)gvtsG_optnumber(L, 7, 10.0);
    if (!ctx->phys || !ctx->scene) { gvts_pushnil(L); return 1; }
    Vec3 pt, nrm;
    float dist = -1.0f;
    bool any = ctx->phys->Gvtcier_Physics_CastRaySurface(*ctx->scene, Vec3(ox, oy, oz), Vec3(dx, dy, dz), len,
                                                         &pt, &nrm, &dist);
    if (!any) { gvts_pushnil(L); return 1; }
    gvts_pushnumber(L, pt.x);
    gvts_pushnumber(L, pt.y);
    gvts_pushnumber(L, pt.z);
    gvts_pushnumber(L, nrm.x);
    gvts_pushnumber(L, nrm.y);
    gvts_pushnumber(L, nrm.z);
    gvts_pushnumber(L, dist);
    return 7;
}

static int g_cr_playerVelocity(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Vec3 v(0, 0, 0);
    if (ctx->phys) v = ctx->phys->Gvtcier_Physics_PlayerVelocity();
    gvts_pushnumber(L, v.x);
    gvts_pushnumber(L, v.y);
    gvts_pushnumber(L, v.z);
    return 3;
}

static int g_cr_resetRagdoll(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->phys && ctx->scene) ctx->phys->Gvtcier_Physics_RebuildSkinned(*ctx->scene);
    return 0;
}

static int g_cr_sensorCreate(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->phys) { gvts_pushboolean(L, 0); return 1; }
    Vec3 c((float)gvtsG_checknumber(L, 1), (float)gvtsG_checknumber(L, 2), (float)gvtsG_checknumber(L, 3));
    Vec3 h((float)gvtsG_checknumber(L, 4), (float)gvtsG_checknumber(L, 5), (float)gvtsG_checknumber(L, 6));
    const char* shape = gvtsG_optstring(L, 7, u8"盒");
    bool sphere = shape && (strcmp(shape, u8"球") == 0 || strcmp(shape, "sphere") == 0);
    gvts_pushboolean(L, ctx->phys->Gvtcier_Physics_CreateSensor(c, h, sphere) ? 1 : 0);
    return 1;
}

static int g_cr_sensorDestroy(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->phys) ctx->phys->Gvtcier_Physics_DestroySensor();
    return 0;
}

static void pushHitList(gvts_State* L, GvtsEngineCtx* ctx, const std::vector<GvtId<Object>>& ids) {
    gvts_createtable(L, (int)ids.size(), 0);
    int n = 0;
    for (GvtId<Object> id : ids) {
        const Object* o = ctx->scene ? ctx->scene->Gvtcier_Scene_GetObject(id) : nullptr;
        if (!o) continue;
        gvts_pushstring(L, o->name.c_str());
        gvts_rawseti(L, -2, ++n);
    }
}

static int g_cr_sensorQuery(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    std::vector<GvtId<Object>> ids;
    if (ctx->phys) ctx->phys->Gvtcier_Physics_QuerySensor(ids);
    pushHitList(L, ctx, ids);
    return 1;
}

static int g_cr_overlapShape(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Vec3 c((float)gvtsG_checknumber(L, 1), (float)gvtsG_checknumber(L, 2), (float)gvtsG_checknumber(L, 3));
    Vec3 h((float)gvtsG_checknumber(L, 4), (float)gvtsG_checknumber(L, 5), (float)gvtsG_checknumber(L, 6));
    const char* shape = gvtsG_optstring(L, 7, u8"盒");
    bool sphere = shape && (strcmp(shape, u8"球") == 0 || strcmp(shape, "sphere") == 0);
    std::vector<GvtId<Object>> ids;
    if (ctx->phys) ctx->phys->Gvtcier_Physics_OverlapShape(c, h, sphere, ids);
    pushHitList(L, ctx, ids);
    return 1;
}

void Gvtcier_Bind_Physics(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"玩家", u8"获取_玩家速度", u8"读取玩家当前速度向量 x, y, z", g_cr_playerVelocity);
    regCap(L, ctx, u8"物理", u8"检测_着地", u8"玩家是否接触地面", g_cr_isGrounded);
    regCap(L, ctx, u8"物理", u8"检测_在平台上", u8"玩家是否站在运动学平台（电梯）上", g_cr_onPlatform);
    regCap(L, ctx, u8"物理", u8"检测_射线命中", u8"返回命中实体名、命中点/法线/距离与唯一 ID，未命中返回 空", g_cr_castRayHit);
    regCap(L, ctx, u8"物理", u8"检测_射线表面", u8"返回命中点/法线/距离，可命中静态几何，未命中返回 空", g_cr_castRaySurface);
    regCap(L, ctx, u8"物理", u8"执行_重置骨骼姿态", u8"重置骨骼模型被物理驱动的姿态", g_cr_resetRagdoll);
    regCap(L, ctx, u8"物理", u8"创建_传感器", u8"创建不阻挡的传感器体：中心XYZ, 半长XYZ, 形状(盒/球)，返回是否成功", g_cr_sensorCreate);
    regCap(L, ctx, u8"物理", u8"删除_传感器", u8"删除当前传感器体", g_cr_sensorDestroy);
    regCap(L, ctx, u8"物理", u8"查询_传感器重叠", u8"返回当前传感器内所有实体名表", g_cr_sensorQuery);
    regCap(L, ctx, u8"物理", u8"检测_形状重叠", u8"一次性查询区域内所有实体名表：中心XYZ, 半长XYZ, 形状(盒/球)", g_cr_overlapShape);
}
