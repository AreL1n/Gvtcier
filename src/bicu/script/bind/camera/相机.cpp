#include "Gvtcier.h"
#include <cstring>

// 脚本绑定：相机。脚本可读写相机的位置、朝向、视场角，以及做射线检测。
// 相机对象由引擎持有（ctx->cam），这里只做参数转换，不接管生命周期。

// 读取相机前方向量（单位向量，三个分量依次压栈）
static int g_cr_getLook(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->cam) {
        Vec3 f = ctx->cam->forward();
        gvts_pushnumber(L, f.x);
        gvts_pushnumber(L, f.y);
        gvts_pushnumber(L, f.z);
        return 3;
    }
    gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushnumber(L, 0);
    return 3;
}

static int g_cr_eyePos(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->player) { gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); return 3; }
    gvts_pushnumber(L, ctx->player->pos.x);
    gvts_pushnumber(L, ctx->player->pos.y + ctx->player->eyeHeight);
    gvts_pushnumber(L, ctx->player->pos.z);
    return 3;
}

static int g_cr_aimPoint(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float d = (float)gvtsG_optnumber(L, 1, 1.0);
    if (!ctx->player) { gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); return 3; }
    Vec3 eye = ctx->player->pos + Vec3(0, ctx->player->eyeHeight, 0);
    Vec3 f = ctx->player->forward();
    gvts_pushnumber(L, eye.x + f.x * d);
    gvts_pushnumber(L, eye.y + f.y * d);
    gvts_pushnumber(L, eye.z + f.z * d);
    return 3;
}

static void viewportSize(GvtsEngineCtx* ctx, float& w, float& h) {
    w = (float)gv::kDefaultWindowWidth;
    h = (float)gv::kDefaultWindowHeight;
    if (ctx->win) { w = (float)ctx->win->width(); h = (float)ctx->win->height(); }
    if (w < 1.0f) w = 1.0f;
    if (h < 1.0f) h = 1.0f;
}

static bool cameraMatrices(GvtsEngineCtx* ctx, Mat4& view, Mat4& proj, float& w, float& h) {
    if (!ctx->cam) return false;
    viewportSize(ctx, w, h);
    view = ctx->cam->viewMatrix();
    proj = ctx->cam->projMatrix(w / h);
    return true;
}

static Vec3 ndcToWorldDir(GvtsEngineCtx* ctx, float sx, float sy, float w, float h) {
    float ndcX = (sx / w) * 2.0f - 1.0f;
    float ndcY = 1.0f - (sy / h) * 2.0f;
    float ndcZ = -1.0f;
    Mat4 view = ctx->cam->viewMatrix();
    Mat4 proj = ctx->cam->projMatrix(w / h);
    Mat4 inv = (proj * view).inverse();
    Vec4 nearP = inv * Vec4(ndcX, ndcY, ndcZ, 1.0f);
    Vec4 farP = inv * Vec4(ndcX, ndcY, 1.0f, 1.0f);
    Vec3 n(nearP.x / nearP.w, nearP.y / nearP.w, nearP.z / nearP.w);
    Vec3 f(farP.x / farP.w, farP.y / farP.w, farP.z / farP.w);
    return (f - n).normalized();
}

static int g_cr_worldToScreen(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float z = (float)gvtsG_checknumber(L, 3);
    Mat4 view, proj;
    float w = 0.0f, h = 0.0f;
    if (!cameraMatrices(ctx, view, proj, w, h)) {
        gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushboolean(L, 0);
        return 3;
    }
    Vec4 p = proj * view * Vec4(x, y, z, 1.0f);
    if (p.w <= 1e-6f) {
        gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushboolean(L, 0);
        return 3;
    }
    float ndcX = p.x / p.w;
    float ndcY = p.y / p.w;
    float sx = (ndcX * 0.5f + 0.5f) * w;
    float sy = (0.5f - ndcY * 0.5f) * h;
    gvts_pushnumber(L, sx);
    gvts_pushnumber(L, sy);
    gvts_pushboolean(L, 1);
    return 3;
}

static int g_cr_screenToWorld(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float sx = (float)gvtsG_checknumber(L, 1);
    float sy = (float)gvtsG_checknumber(L, 2);
    float d = (float)gvtsG_optnumber(L, 3, 1.0);
    Mat4 view, proj;
    float w = 0.0f, h = 0.0f;
    if (!cameraMatrices(ctx, view, proj, w, h)) {
        gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushnumber(L, 0);
        return 3;
    }
    Vec3 dir = ndcToWorldDir(ctx, sx, sy, w, h);
    Vec3 eye = ctx->cam->pos;
    gvts_pushnumber(L, eye.x + dir.x * d);
    gvts_pushnumber(L, eye.y + dir.y * d);
    gvts_pushnumber(L, eye.z + dir.z * d);
    return 3;
}

static int g_cr_screenRayDir(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float sx = (float)gvtsG_checknumber(L, 1);
    float sy = (float)gvtsG_checknumber(L, 2);
    Mat4 view, proj;
    float w = 0.0f, h = 0.0f;
    if (!cameraMatrices(ctx, view, proj, w, h)) {
        gvts_pushnumber(L, 0); gvts_pushnumber(L, 0); gvts_pushnumber(L, 0);
        return 3;
    }
    Vec3 dir = ndcToWorldDir(ctx, sx, sy, w, h);
    gvts_pushnumber(L, dir.x);
    gvts_pushnumber(L, dir.y);
    gvts_pushnumber(L, dir.z);
    return 3;
}

static int g_cr_viewProj(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Mat4 view, proj;
    float w = 0.0f, h = 0.0f;
    if (!cameraMatrices(ctx, view, proj, w, h)) {
        gvts_pushnil(L);
        return 1;
    }
    Vec3 eye = ctx->cam->pos;
    Vec3 f = ctx->cam->forward();
    gvts_pushnumber(L, view.m[0]);
    gvts_pushnumber(L, view.m[4]);
    gvts_pushnumber(L, view.m[8]);
    gvts_pushnumber(L, view.m[12]);
    gvts_pushnumber(L, view.m[1]);
    gvts_pushnumber(L, view.m[5]);
    gvts_pushnumber(L, view.m[9]);
    gvts_pushnumber(L, view.m[13]);
    gvts_pushnumber(L, view.m[2]);
    gvts_pushnumber(L, view.m[6]);
    gvts_pushnumber(L, view.m[10]);
    gvts_pushnumber(L, view.m[14]);
    gvts_pushnumber(L, view.m[3]);
    gvts_pushnumber(L, view.m[7]);
    gvts_pushnumber(L, view.m[11]);
    gvts_pushnumber(L, view.m[15]);
    gvts_pushnumber(L, proj.m[0]);
    gvts_pushnumber(L, proj.m[4]);
    gvts_pushnumber(L, proj.m[8]);
    gvts_pushnumber(L, proj.m[12]);
    gvts_pushnumber(L, proj.m[1]);
    gvts_pushnumber(L, proj.m[5]);
    gvts_pushnumber(L, proj.m[9]);
    gvts_pushnumber(L, proj.m[13]);
    gvts_pushnumber(L, proj.m[2]);
    gvts_pushnumber(L, proj.m[6]);
    gvts_pushnumber(L, proj.m[10]);
    gvts_pushnumber(L, proj.m[14]);
    gvts_pushnumber(L, proj.m[3]);
    gvts_pushnumber(L, proj.m[7]);
    gvts_pushnumber(L, proj.m[11]);
    gvts_pushnumber(L, proj.m[15]);
    gvts_pushnumber(L, w);
    gvts_pushnumber(L, h);
    gvts_pushnumber(L, eye.x);
    gvts_pushnumber(L, eye.y);
    gvts_pushnumber(L, eye.z);
    gvts_pushnumber(L, f.x);
    gvts_pushnumber(L, f.y);
    gvts_pushnumber(L, f.z);
    gvts_pushnumber(L, ctx->cam->fov);
    gvts_pushnumber(L, ctx->cam->rollRad);
    gvts_pushnumber(L, ctx->cam->nearPlane);
    gvts_pushnumber(L, ctx->cam->farPlane);
    return 46;
}

static int g_cam_setMode(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* s = gvtsG_checkstring(L, 1);
    int mode = 0;
    if (std::strcmp(s, u8"工程") == 0) mode = 1;
    else if (std::strcmp(s, u8"玩家") == 0) mode = 0;
    else mode = (gvtsG_optnumber(L, 1, 0.0) > 0.5) ? 1 : 0;
    if (ctx->cam) ctx->cam->mode = mode;
    return 0;
}

static int g_cam_getMode(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int mode = ctx->cam ? ctx->cam->mode : 0;
    gvts_pushstring(L, mode == 1 ? u8"工程" : u8"玩家");
    return 1;
}

static int g_cam_setFlySpeed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float v = (float)gvtsG_optnumber(L, 1, 12.0);
    if (v < 0.1f) v = 0.1f;
    if (ctx->cam) ctx->cam->flySpeed = v;
    return 0;
}

static int g_cam_setTarget(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->cam) return 0;
    if (gvts_isnoneornil(L, 1)) { ctx->cam->target.clear(); return 0; }
    const char* s = gvtsG_checkstring(L, 1);
    ctx->cam->target = s ? s : "";
    return 0;
}

static int g_cam_setOffset(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->cam) return 0;
    float x = (float)gvtsG_optnumber(L, 1, 0.0);
    float y = (float)gvtsG_optnumber(L, 2, 0.0);
    float z = (float)gvtsG_optnumber(L, 3, 0.0);
    ctx->cam->offset = Vec3(x, y, z);
    return 0;
}

static int g_cam_getOffset(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Vec3 o = ctx->cam ? ctx->cam->offset : Vec3(0, 0, 0);
    gvts_pushnumber(L, o.x);
    gvts_pushnumber(L, o.y);
    gvts_pushnumber(L, o.z);
    return 3;
}

static int g_cam_setAngles(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->cam) return 0;
    if (!gvts_isnoneornil(L, 1)) ctx->cam->yawDeg = (float)gvtsG_checknumber(L, 1);
    if (!gvts_isnoneornil(L, 2)) ctx->cam->pitchDeg = (float)gvtsG_checknumber(L, 2);
    return 0;
}

void Gvtcier_Bind_Camera(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"玩家", u8"获取_相机前方", u8"取相机朝向单位向量", g_cr_getLook);
    regCap(L, ctx, u8"玩家", u8"获取_视点位置", u8"读取玩家眼睛的世界坐标", g_cr_eyePos);
    regCap(L, ctx, u8"玩家", u8"获取_瞄准点", u8"取玩家眼睛沿视线 d 米处的世界坐标，默认 1 米", g_cr_aimPoint);
    regCap(L, ctx, u8"玩家", u8"获取_屏幕转世界", u8"屏幕像素加深度 d 映射为世界坐标，d 默认 1，d<=0 表示按相机前方距离", g_cr_screenToWorld);
    regCap(L, ctx, u8"玩家", u8"获取_世界转屏幕", u8"世界坐标投影为屏幕像素，第三人返回是否在相机前方", g_cr_worldToScreen);
    regCap(L, ctx, u8"玩家", u8"获取_屏幕射线方向", u8"屏幕像素对应的世界射线单位方向", g_cr_screenRayDir);
    regCap(L, ctx, u8"玩家", u8"获取_相机矩阵", u8"返回当前渲染相机的视图/投影矩阵与相机参数", g_cr_viewProj);
    regCap(L, ctx, u8"相机", u8"设置_相机", u8"切换相机模式，传 \"玩家\" 为玩家相机，传 \"工程\" 为可飞行的工程相机", g_cam_setMode);
    regCap(L, ctx, u8"相机", u8"获取_相机", u8"返回当前相机模式字符串：\"玩家\" 或 \"工程\"", g_cam_getMode);
    regCap(L, ctx, u8"相机", u8"设置_飞行速度", u8"设置工程相机飞行速度，默认 12", g_cam_setFlySpeed);
    regCap(L, ctx, u8"相机", u8"设置_相机目标", u8"让相机跟随指定名字的实体，传 空 取消跟随", g_cam_setTarget);
    regCap(L, ctx, u8"相机", u8"设置_相机偏移", u8"相机相对跟随目标的偏移 x,y,z（第三人称后拉）", g_cam_setOffset);
    regCap(L, ctx, u8"相机", u8"获取_相机偏移", u8"返回当前相机偏移 x,y,z", g_cam_getOffset);
    regCap(L, ctx, u8"相机", u8"设置_相机角度", u8"直接设置相机 yaw/pitch 角度（度）", g_cam_setAngles);
}
