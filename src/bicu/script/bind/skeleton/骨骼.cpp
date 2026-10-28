#include "Gvtcier.h"

// 脚本绑定：骨骼。脚本查询与操作蒙皮模型的骨骼（世界变换、绑定姿态等），
// 常用于挂点（把武器绑到手上）或做程序化动画。

// 读取网格的局部半尺寸（包围盒的一半），用于碰撞与摆放计算
static int g_cr_meshHalfExtents(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    if (!ctx->scene || !o || !ctx->scene->Gvtcier_Scene_GetMesh(o->mesh)) { gvts_pushnil(L); return 1; }
    Vec3 h = ctx->scene->Gvtcier_Scene_MeshHalfExtents(o->mesh);
    gvts_pushnumber(L, h.x);
    gvts_pushnumber(L, h.y);
    gvts_pushnumber(L, h.z);
    return 3;
}

static int g_cr_vertexCount(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    const Mesh* m = (o && ctx->scene) ? ctx->scene->Gvtcier_Scene_GetMesh(o->mesh) : nullptr;
    if (!m) { gvts_pushinteger(L, 0); return 1; }
    gvts_pushinteger(L, (gvts_Integer)m->cpuVerts().size());
    return 1;
}

static int g_cr_vertexWorld(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    const Mesh* m = (o && ctx->scene) ? ctx->scene->Gvtcier_Scene_GetMesh(o->mesh) : nullptr;
    if (!m) { gvts_pushnil(L); return 1; }
    int i = (int)gvtsG_checkinteger(L, 2);
    const std::vector<Vertex>& vs = m->cpuVerts();
    if (i < 1 || i > (int)vs.size()) { gvts_pushnil(L); return 1; }
    Vec3 w = o->Gvtcier_Object_Matrix() * vs[(size_t)i - 1].pos;
    gvts_pushnumber(L, w.x);
    gvts_pushnumber(L, w.y);
    gvts_pushnumber(L, w.z);
    return 3;
}

static int g_skin_load(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* path = gvtsG_checkstring(L, 1);
    float x = (float)gvtsG_optnumber(L, 2, 0.0);
    float y = (float)gvtsG_optnumber(L, 3, 0.0);
    float z = (float)gvtsG_optnumber(L, 4, 0.0);
    if (!ctx->scene) { gvts_pushboolean(L, 0); return 1; }
    bool ok = ctx->scene->Gvtcier_Scene_LoadSkinnedFile(path, Vec3(x, y, z));
    if (!ok) printf(u8"[gvts] 骨骼模型加载失败: %s\n", path);
    else if (ctx->phys) ctx->phys->Gvtcier_Physics_RebuildSkinned(*ctx->scene);
    gvts_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int g_skin_count(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushinteger(L, ctx->scene ? ctx->scene->Gvtcier_Scene_AnimCount() : 0);
    return 1;
}

static int g_skin_name(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!gvts_isnumber(L, 1)) { gvts_pushnil(L); return 1; }
    int i = (int)gvts_tointeger(L, 1);
    gvts_pushstring(L, ctx->scene ? ctx->scene->Gvtcier_Scene_AnimName(i) : "");
    return 1;
}

static int g_skin_duration(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!gvts_isnumber(L, 1)) { gvts_pushnil(L); return 1; }
    int i = (int)gvts_tointeger(L, 1);
    gvts_pushnumber(L, ctx->scene ? ctx->scene->Gvtcier_Scene_AnimDuration(i) : 0.0);
    return 1;
}

static int g_skin_play(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!gvts_isnumber(L, 1)) { gvts_pushboolean(L, 0); return 1; }
    int i = (int)gvts_tointeger(L, 1);
    bool loop = gvts_isnoneornil(L, 2) ? true : (gvts_toboolean(L, 2) != 0);
    bool ok = ctx->scene && ctx->scene->Gvtcier_Scene_AnimPlay(i, loop);
    gvts_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int g_skin_stop(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->scene) ctx->scene->Gvtcier_Scene_AnimStop();
    return 0;
}

static int g_skin_playing(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushboolean(L, ctx->scene && ctx->scene->Gvtcier_Scene_AnimPlaying());
    return 1;
}

static int g_skin_speed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) { gvts_pushnumber(L, ctx->scene ? ctx->scene->Gvtcier_Scene_AnimSpeed() : 1.0); return 1; }
    if (ctx->scene) ctx->scene->Gvtcier_Scene_AnimSetSpeed((float)gvtsG_checknumber(L, 1));
    return 0;
}

static int g_skin_time(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (gvts_isnoneornil(L, 1)) { gvts_pushnumber(L, ctx->scene ? ctx->scene->Gvtcier_Scene_AnimTime() : 0.0); return 1; }
    if (ctx->scene) ctx->scene->Gvtcier_Scene_AnimSetTime((float)gvtsG_checknumber(L, 1));
    return 0;
}

static int g_skin_pos(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float z = (float)gvtsG_checknumber(L, 3);
    if (ctx->scene) ctx->scene->Gvtcier_Scene_AnimSetPos(Vec3(x, y, z));
    return 0;
}

static int g_skin_bones(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushinteger(L, ctx->scene ? (int)ctx->scene->skinnedMesh().bones.size() : 0);
    return 1;
}

static int g_model_load(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) { gvts_pushnil(L); return 1; }
    const char* name = gvtsG_checkstring(L, 1);
    const char* path = gvtsG_checkstring(L, 2);
    float x = (float)gvtsG_optnumber(L, 3, 0.0);
    float y = (float)gvtsG_optnumber(L, 4, 0.0);
    float z = (float)gvtsG_optnumber(L, 5, 0.0);
    Scene* sc = ctx->scene;
    {
        GvtId<Object> found = sc->Gvtcier_Scene_FindByName(name);
        if (found.valid()) {
            pushObjHandle(L, ctx, found, name);
            return 1;
        }
    }
    Object o;
    o.name = name;
    o.kind = ObjKind::Model;
    o.modelName = path;
    o.tex = sc->whiteTex();
    o.castShadow = true;
    o.pos = Vec3(x, y, z);
    o.collider = ColliderType::Box;
    GvtId<Mesh> m = sc->Gvtcier_Scene_LoadModelObject(path);
    if (!m.valid()) {
        printf(u8"[gvts] 模型加载失败: %s\n", path);
        gvts_pushnil(L);
        return 1;
    }
    o.mesh = m;
    GvtId<Object> id = sc->Gvtcier_Scene_RegisterObject(std::move(o));
    Object* no = sc->Gvtcier_Scene_GetObject(id);
    if (ctx->phys && no) ctx->phys->Gvtcier_Physics_AddObject(*no, id);
    pushObjHandle(L, ctx, id, name);
    return 1;
}

static int g_model_scalePivot(gvts_State* L) {
    Object* o = argObject(L, 1);
    if (!o) return 0;
    if (gvts_isnoneornil(L, 2)) {
        gvts_pushnumber(L, o->pivot.x);
        gvts_pushnumber(L, o->pivot.y);
        gvts_pushnumber(L, o->pivot.z);
        return 3;
    }
    o->pivot = Vec3((float)gvtsG_checknumber(L, 2),
                    (float)gvtsG_checknumber(L, 3),
                    (float)gvtsG_checknumber(L, 4));
    o->Gvtcier_Object_MarkXformDirty();
    return 0;
}

void Gvtcier_Bind_Skeleton(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"场景", u8"获取_网格半长", u8"取实体网格半长，返回 XYZ", g_cr_meshHalfExtents);
    regCap(L, ctx, u8"场景", u8"获取_顶点数量", u8"取实体网格顶点总数，用于遍历校验真实尺寸", g_cr_vertexCount);
    regCap(L, ctx, u8"场景", u8"获取_顶点世界坐标", u8"取第 N 个顶点的世界坐标 XYZ（含缩放与旋转），N 从 1 起", g_cr_vertexWorld);
    regCap(L, ctx, u8"骨骼", u8"加载_骨骼模型", u8"从 glb 文件加载骨骼模型并放置到坐标", g_skin_load);
    regCap(L, ctx, u8"骨骼", u8"获取_骨骼数量", u8"返回骨骼模型骨骼根数", g_skin_bones);
    regCap(L, ctx, u8"骨骼", u8"获取_动画数量", u8"返回模型内动画条数", g_skin_count);
    regCap(L, ctx, u8"骨骼", u8"获取_动画名称", u8"按序号取动画名", g_skin_name);
    regCap(L, ctx, u8"骨骼", u8"获取_动画时长", u8"按序号取动画时长秒", g_skin_duration);
    regCap(L, ctx, u8"骨骼", u8"播放_动画", u8"按序号播放动画，可指定是否循环", g_skin_play);
    regCap(L, ctx, u8"骨骼", u8"停止_动画", u8"停止当前动画并保持姿势", g_skin_stop);
    regCap(L, ctx, u8"骨骼", u8"检测_动画播放中", u8"查询动画是否在播放", g_skin_playing);
    regCap(L, ctx, u8"骨骼", u8"设置_动画速度", u8"读写动画播放速度倍率，无参读取", g_skin_speed);
    regCap(L, ctx, u8"骨骼", u8"设置_动画进度", u8"读写动画当前时间秒，无参读取", g_skin_time);
    regCap(L, ctx, u8"骨骼", u8"设置_骨骼模型位置", u8"设置骨骼模型世界坐标", g_skin_pos);
    regCap(L, ctx, u8"模型", u8"加载_模型", u8"从 obj 或 glb 文件加载模型为实体，返回实体句柄", g_model_load);
    regCap(L, ctx, u8"模型", u8"设置_缩放锚点", u8"读写缩放中心偏移（模型空间），缩放围绕该点进行，无参读取返回 XYZ", g_model_scalePivot);
}
