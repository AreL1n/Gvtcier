#include "Gvtcier.h"

// 脚本绑定：玩家。读写玩家的位置、速度、朝向、视场角等。
// 玩家对象由引擎持有（ctx->player），生命周期覆盖整个游戏会话。

// 读取/写入玩家位置。无参时读取（压栈三个分量），有参时写入
static int g_cr_playerPos(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->player) return 0;
    if (gvts_isnumber(L, 1)) {
        float x = (float)gvtsG_checknumber(L, 1);
        float y = (float)gvtsG_checknumber(L, 2);
        float z = (float)gvtsG_checknumber(L, 3);
        if (ctx->phys) ctx->phys->Gvtcier_Physics_SetPlayerPos(Vec3(x, y, z));
        ctx->player->pos = Vec3(x, y, z);
        return 0;
    }
    gvts_pushnumber(L, ctx->player->pos.x);
    gvts_pushnumber(L, ctx->player->pos.y);
    gvts_pushnumber(L, ctx->player->pos.z);
    return 3;
}

static int g_playerSetEyeHeight(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float v = (float)gvtsG_checknumber(L, 1);
    if (ctx->player) ctx->player->eyeHeight = v;
    return 0;
}

static int g_playerSetFov(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float v = (float)gvtsG_checknumber(L, 1);
    if (ctx->player) ctx->player->fov = v;
    if (ctx->cam) ctx->cam->fov = v;
    return 0;
}

static int g_cameraRoll(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->cam) return 0;
    if (gvts_isnoneornil(L, 1)) { gvts_pushnumber(L, ctx->cam->rollRad); return 1; }
    ctx->cam->rollRad = (float)gvtsG_checknumber(L, 1);
    return 0;
}

static int g_playerSetSpeed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float v = (float)gvtsG_checknumber(L, 1);
    if (ctx->player) ctx->player->speed = v;
    if (ctx->phys) ctx->phys->Gvtcier_Physics_SetPlayerSpeed(v);
    return 0;
}

static int g_walkRunSpeed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->phys) { gvts_pushnumber(L, kPlayerWalkSpeed); gvts_pushnumber(L, kPlayerRunSpeed); return 2; }
    if (gvts_isnumber(L, 1)) {
        float walk = (float)gvtsG_checknumber(L, 1);
        float run = (float)gvtsG_optnumber(L, 2, 0.0);
        ctx->phys->Gvtcier_Physics_SetWalkRunSpeed(walk, run);
    }
    float w = 0.0f, r = 0.0f;
    ctx->phys->Gvtcier_Physics_GetWalkRunSpeed(&w, &r);
    gvts_pushnumber(L, w);
    gvts_pushnumber(L, r);
    return 2;
}

static int g_runEnabled(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isboolean(L, 1)) {
        if (ctx->phys) ctx->phys->Gvtcier_Physics_SetRunEnabled(gvts_toboolean(L, 1) != 0);
    }
    gvts_pushboolean(L, ctx->phys && ctx->phys->Gvtcier_Physics_RunEnabled());
    return 1;
}

static int g_isRunning(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushboolean(L, ctx->player && ctx->player->running);
    return 1;
}

static int g_playerSetJump(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float v = (float)gvtsG_checknumber(L, 1);
    if (ctx->phys) ctx->phys->Gvtcier_Physics_SetPlayerJump(v);
    return 0;
}

static int g_playerSetView(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) {
        gvts_pushnumber(L, ctx->player ? ctx->player->yawDeg : 0.0);
        gvts_pushnumber(L, ctx->player ? ctx->player->pitchDeg : 0.0);
        return 2;
    }
    float yaw = (float)gvtsG_checknumber(L, 1);
    float pitch = (float)gvtsG_checknumber(L, 2);
    if (ctx->player) { ctx->player->yawDeg = yaw; ctx->player->pitchDeg = pitch; }
    if (ctx->cam) { ctx->cam->yawDeg = yaw; ctx->cam->pitchDeg = pitch; }
    return 0;
}

void Gvtcier_Bind_Player(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"玩家", u8"获取_玩家位置", u8"读写玩家坐标 XYZ，无参读取返回 x, y, z", g_cr_playerPos);
    regCap(L, ctx, u8"玩家", u8"设置_锁定速度", u8"设置玩家锁定移动速度，传入值持续生效且优先于走/跑速度", g_playerSetSpeed);
    regCap(L, ctx, u8"玩家", u8"设置_跳跃力", u8"设置玩家跳跃初速", g_playerSetJump);
    regCap(L, ctx, u8"玩家", u8"设置_移动速度", u8"读写走路与奔跑速度：无参读取返回 走速, 跑速；传参则设置", g_walkRunSpeed);
    regCap(L, ctx, u8"玩家", u8"设置_奔跑开关", u8"读写 Shift 奔跑机制开关（关后恒为走路速度），无参读取", g_runEnabled);
    regCap(L, ctx, u8"玩家", u8"检测_正在奔跑", u8"查询本帧玩家是否处于奔跑状态", g_isRunning);
    regCap(L, ctx, u8"玩家", u8"设置_视角", u8"读写玩家偏航与俯仰角，无参读取返回 yaw, pitch", g_playerSetView);
    regCap(L, ctx, u8"玩家", u8"设置_视高", u8"设置玩家眼睛高度，蹲下爬梯用", g_playerSetEyeHeight);
    regCap(L, ctx, u8"玩家", u8"设置_视场", u8"设置玩家相机视场角，开镜用", g_playerSetFov);
    regCap(L, ctx, u8"玩家", u8"设置_相机侧倾", u8"读写相机横滚弧度，无参读取", g_cameraRoll);
}
