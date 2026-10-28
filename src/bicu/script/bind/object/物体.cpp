#include "Gvtcier.h"

// 脚本绑定：物体。脚本通过句柄操作场景对象（位置、旋转、缩放、颜色、纹理等）。
// 句柄 ObjHandle 内部存的是 GvtId，不是裸指针 —— 对象被销毁后句柄自动失效，
// objFromHandle 取不到对象就返回空，脚本侧表现为操作无效，不会崩溃。

// 从句柄解析出对象指针。句柄无效、世代号过期或场景已销毁时返回 nullptr
Object* objFromHandle(ObjHandle* h) {
    if (!h || !h->ctx || !h->ctx->scene) return nullptr;
    Scene* sc = h->ctx->scene;
    Object* o = sc->Gvtcier_Scene_GetObject(h->id);
    if (!o) return nullptr;
    if (strncmp(o->name.c_str(), h->name, sizeof(h->name)) != 0) return nullptr;
    return o;
}

static const char* objKindStr(ObjKind k) {
    switch (k) {
    case ObjKind::Geometry:    return "Geometry";
    case ObjKind::LightPoint:  return "LightPoint";
    case ObjKind::LightDir:    return "LightDir";
    case ObjKind::LightSpot:   return "LightSpot";
    case ObjKind::Trigger:     return "Trigger";
    case ObjKind::Audio:       return "Audio";
    case ObjKind::Image:       return "Image";
    case ObjKind::Effect:      return "Effect";
    case ObjKind::Player:      return "Player";
    case ObjKind::Camera:      return "Camera";
    case ObjKind::EffectEmitter: return "EffectEmitter";
    }
    return "Unknown";
}

static int obj_getPos(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    gvts_pushnumber(L, op->pos.x);
    gvts_pushnumber(L, op->pos.y);
    gvts_pushnumber(L, op->pos.z);
    return 3;
}

static int obj_setPos(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    float x = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float z = (float)gvtsG_checknumber(L, 4);
    op->pos = Vec3(x, y, z);
    op->Gvtcier_Object_MarkXformDirty();
    if (op->kind != ObjKind::Trigger && h->ctx->phys)
        h->ctx->phys->Gvtcier_Physics_SetObjectPos(h->id, Vec3(x, y, z));
    return 0;
}

static int obj_getRot(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    gvts_pushnumber(L, op->rotDeg.x);
    gvts_pushnumber(L, op->rotDeg.y);
    gvts_pushnumber(L, op->rotDeg.z);
    return 3;
}

static int obj_setRot(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    float p = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float r = (float)gvtsG_checknumber(L, 4);
    op->rotDeg = Vec3(p, y, r);
    op->Gvtcier_Object_MarkXformDirty();
    return 0;
}

static int obj_getScale(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    gvts_pushnumber(L, op->scale.x);
    gvts_pushnumber(L, op->scale.y);
    gvts_pushnumber(L, op->scale.z);
    return 3;
}

static int obj_setScale(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    op->scale.x = (float)gvtsG_checknumber(L, 2);
    op->scale.y = (float)gvtsG_optnumber(L, 3, op->scale.x);
    op->scale.z = (float)gvtsG_optnumber(L, 4, op->scale.x);
    op->Gvtcier_Object_MarkXformDirty();
    return 0;
}

static int obj_getName(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    gvts_pushstring(L, op->name.c_str());
    return 1;
}

static int obj_getKind(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) return 0;
    gvts_pushstring(L, objKindStr(op->kind));
    return 1;
}

static int obj_isVisible(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, 1);
    Object* op = objFromHandle(h);
    if (!op) { gvts_pushboolean(L, 0); return 1; }
    gvts_pushboolean(L, op->visible);
    return 1;
}

static const char* kObjMeta = u8"Gvtcier_对象";

static const gvtsG_Reg objMethods[] = {
    {u8"取位置", obj_getPos},
    {u8"设位置", obj_setPos},
    {u8"取旋转", obj_getRot},
    {u8"设旋转", obj_setRot},
    {u8"取缩放", obj_getScale},
    {u8"设缩放", obj_setScale},
    {u8"取名称", obj_getName},
    {u8"取类型", obj_getKind},
    {u8"是否可见", obj_isVisible},
    {u8"播放特效", obj_playEffect},
    {u8"停止特效", obj_stopEffect},
    {NULL, NULL}
};

void pushObjHandle(gvts_State* L, GvtsEngineCtx* ctx, GvtId<Object> id, const char* name) {
    ObjHandle* h = (ObjHandle*)gvts_newuserdata(L, sizeof(ObjHandle));
    h->ctx = ctx;
    h->id = id;
    strncpy(h->name, name ? name : "", sizeof(h->name) - 1);
    h->name[sizeof(h->name) - 1] = 0;
    if (gvtsG_newmetatable(L, kObjMeta)) {
        gvtsG_setfuncs(L, objMethods, 0);
        gvts_pushcfunction(L, g_obj_index);
        gvts_setfield(L, -2, "__index");
        gvts_pushcfunction(L, g_obj_newindex);
        gvts_setfield(L, -2, "__newindex");
    }
    gvts_setmetatable(L, -2);
}

Object* argObject(gvts_State* L, int idx) {
    ObjHandle* h = (ObjHandle*)gvts_touserdata(L, idx);
    return objFromHandle(h);
}

static int g_cr_attribution(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx->scene) { gvts_pushnil(L); return 1; }
    Scene* sc = ctx->scene;
    GvtId<Object> found = sc->Gvtcier_Scene_FindByName(name);
    if (found.valid()) {
        pushObjHandle(L, ctx, found, name);
        ctx->ownerHandle = gvts_touserdata(L, -1);
        return 1;
    }
    printf("[gvts] 句柄: 未找到物体 '%s'\n", name);
    gvts_pushnil(L);
    return 1;
}

int g_obj_index(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvtsG_checkudata(L, 1, kObjMeta);
    const char* k = gvtsG_checkstring(L, 2);
    if (strcmp(k, u8"名字") == 0 || strcmp(k, "name") == 0) { gvts_pushstring(L, h->name); return 1; }
    gvts_getmetatable(L, 1);
    gvts_pushvalue(L, 2);
    gvts_rawget(L, -2);
    return 1;
}

int g_obj_newindex(gvts_State* L) {
    ObjHandle* h = (ObjHandle*)gvtsG_checkudata(L, 1, kObjMeta);
    const char* k = gvtsG_checkstring(L, 2);
    (void)h; (void)k;
    return 0;
}

void Gvtcier_Bind_Object(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"脚本", u8"创建_句柄", u8"绑定物体句柄", g_cr_attribution);
}
