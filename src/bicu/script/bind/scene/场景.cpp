#include "Gvtcier.h"

// 实体类型的中文名表，下标必须与 ObjKind 枚举的取值一一对应（共 13 种）。
// 顺序：几何、点光、方向光、聚光、触发、音频、图像、特效、发射器、玩家、相机、文本、模型。
// 快照里的"类型"字段直接从这里取，所以枚举一改动这张表就要跟着改。
static const char* const kObjKindNames[] = {
    u8"几何", u8"点光", u8"方向光", u8"聚光", u8"触发", u8"音频",
    u8"图像", u8"特效", u8"发射器", u8"玩家", u8"相机", u8"文本", u8"模型"
};

// 脚本名：获取_实体快照(名字)
// 参数：实体名（先按 name 精确查，查不到再按 uid 查）。
// 返回：一张字段表（名字/ID/标签/类型/位置XYZ/旋转XYZ/缩放XYZ/锚点XYZ/可见/碰撞/刚体/运动学/质量/
//       顶点数/纹理/父级），实体不存在返回 空。
// 用途：一次拿到实体的全部真实字段做调试或存档，比逐个调 获取_属性 省事。
// 调用：Scene::Gvtcier_Scene_FindByName / FindByUid / GetObject / GetMesh。
static int g_entity_snapshot(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !ctx->scene || !name || !name[0]) { gvts_pushnil(L); return 1; }
    GvtId<Object> found = ctx->scene->Gvtcier_Scene_FindByName(name);
    if (!found.valid()) found = ctx->scene->Gvtcier_Scene_FindByUid(name);
    Object* op = ctx->scene->Gvtcier_Scene_GetObject(found);
    if (!op) { gvts_pushnil(L); return 1; }
    const Object& o = *op;
    gvts_newtable(L);
    gvts_pushstring(L, o.name.c_str()); gvts_setfield(L, -2, u8"名字");
    gvts_pushstring(L, o.uid.c_str()); gvts_setfield(L, -2, u8"ID");
    gvts_pushstring(L, o.tag.c_str()); gvts_setfield(L, -2, u8"标签");
    int kind = (int)o.kind;
    gvts_pushstring(L, (kind >= 0 && kind < 13) ? kObjKindNames[kind] : u8"未知");
    gvts_setfield(L, -2, u8"类型");
    gvts_pushnumber(L, o.pos.x); gvts_setfield(L, -2, u8"位置X");
    gvts_pushnumber(L, o.pos.y); gvts_setfield(L, -2, u8"位置Y");
    gvts_pushnumber(L, o.pos.z); gvts_setfield(L, -2, u8"位置Z");
    gvts_pushnumber(L, o.rotDeg.x); gvts_setfield(L, -2, u8"旋转X");
    gvts_pushnumber(L, o.rotDeg.y); gvts_setfield(L, -2, u8"旋转Y");
    gvts_pushnumber(L, o.rotDeg.z); gvts_setfield(L, -2, u8"旋转Z");
    gvts_pushnumber(L, o.scale.x); gvts_setfield(L, -2, u8"缩放X");
    gvts_pushnumber(L, o.scale.y); gvts_setfield(L, -2, u8"缩放Y");
    gvts_pushnumber(L, o.scale.z); gvts_setfield(L, -2, u8"缩放Z");
    gvts_pushnumber(L, o.pivot.x); gvts_setfield(L, -2, u8"锚点X");
    gvts_pushnumber(L, o.pivot.y); gvts_setfield(L, -2, u8"锚点Y");
    gvts_pushnumber(L, o.pivot.z); gvts_setfield(L, -2, u8"锚点Z");
    gvts_pushboolean(L, o.visible); gvts_setfield(L, -2, u8"可见");
    gvts_pushinteger(L, (int)o.collider); gvts_setfield(L, -2, u8"碰撞");
    gvts_pushboolean(L, o.rigidBody); gvts_setfield(L, -2, u8"刚体");
    gvts_pushboolean(L, o.kinematic); gvts_setfield(L, -2, u8"运动学");
    gvts_pushnumber(L, o.mass); gvts_setfield(L, -2, u8"质量");
    gvts_pushinteger(L, (gvts_Integer)(o.mesh.valid() && ctx->scene->Gvtcier_Scene_GetMesh(o.mesh) ? ctx->scene->Gvtcier_Scene_GetMesh(o.mesh)->cpuVerts().size() : 0));
    gvts_setfield(L, -2, u8"顶点数");
    gvts_pushstring(L, o.texName.c_str()); gvts_setfield(L, -2, u8"纹理");
    {
        const Object* po = ctx->scene->Gvtcier_Scene_GetObject(o.parent);
        if (po) gvts_pushstring(L, po->name.c_str());
        else gvts_pushnil(L);
    }
    gvts_setfield(L, -2, u8"父级");
    return 1;
}

// 脚本名：检测_场景问题()
// 参数：无。
// 返回：一个字符串数组，每条是"实体名: 问题描述"；空表表示场景健康。
// 检查项：缺ID、重ID、重名、位置出现 NaN、缩放非正、父级自引用/越界/成环、可见模型缺网格。
// 两个遍历：第一遍统计名字与 ID 出现次数，第二遍逐实体比对并拼问题串。
// 这一步只读不改，可以放心在运行时随便调。
static int g_scene_validate(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_newtable(L);
    if (!ctx || !ctx->scene) return 1;
    std::map<std::string, int> nameCount;
    std::map<std::string, int> uidCount;
    ctx->scene->ForEachObject([&](const Object& o, GvtId<Object>) {
        nameCount[o.name]++;
        uidCount[o.uid]++;
    });
    int out = 0;
    ctx->scene->ForEachObject([&](const Object& o, GvtId<Object> id) {
        std::string issues;
        if (o.uid.empty()) issues += u8"缺ID ";
        else if (uidCount[o.uid] > 1) issues += u8"重ID ";
        if (nameCount[o.name] > 1) issues += u8"重名 ";
        if (std::isnan(o.pos.x) || std::isnan(o.pos.y) || std::isnan(o.pos.z))
            issues += u8"位置NaN ";
        if (o.scale.x <= 0.0f || o.scale.y <= 0.0f || o.scale.z <= 0.0f)
            issues += u8"缩放非法 ";
        if (o.parent.valid()) {
            if (o.parent == id) issues += u8"父级自引用 ";
            else if (!ctx->scene->Gvtcier_Scene_GetObject(o.parent)) issues += u8"父级越界 ";
            else if (ctx->scene->Gvtcier_Scene_IsAncestor(id, o.parent)) issues += u8"父级成环 ";
        }
        if (o.visible && !ctx->scene->Gvtcier_Scene_GetMesh(o.mesh) && o.kind == ObjKind::Model) issues += u8"模型缺网格 ";
        if (issues.empty()) return;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s: %s", o.name.c_str(), issues.c_str());
        gvts_pushstring(L, buf);
        gvts_rawseti(L, -2, ++out);
    });
    return 1;
}

// 脚本名：执行_刷新实体()
// 参数：无。返回：无。
// 调用：Scene::Gvtcier_Scene_RefreshTransforms，立刻重算层级变换。
// 平时变换是延迟到下一帧统一算的，需要"改了父级马上读子级世界坐标"时用这个强制刷新。
static int g_cr_refresh(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->scene) ctx->scene->Gvtcier_Scene_RefreshTransforms();
    return 0;
}

// 脚本名：删除_场景内容()
// 参数：无。返回：无。
// 调用：Scene::Gvtcier_Scene_ClearObjects，清空所有物体。
// 注意这里不通知物理系统，清完场景里可能还留着物理体，配 加载_地图 用没问题（那边会处理物理）。
static int g_scene_clear(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->scene) ctx->scene->Gvtcier_Scene_ClearObjects();
    return 0;
}

// 脚本名：创建_实体副本(源实体, 新名字)
// 参数：源实体句柄；新实体的名字。
// 返回：新实体句柄；同名实体已存在时直接返回已有的那个（幂等）；参数非法返回 空。
// 拷贝内容：源实体的全部字段，但 uid 会被清空（交给场景重新分配唯一 ID）、父级被断开（挂到根下）。
// 注册后立刻加进物理世界，所以副本会立刻参与碰撞。
static int g_cr_cloneEntity(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* src = argObject(L, 1);
    const char* newName = gvtsG_checkstring(L, 2);
    if (!src || !ctx->scene || !newName) { gvts_pushnil(L); return 1; }
    Scene* sc = ctx->scene;
    GvtId<Object> exist = sc->Gvtcier_Scene_FindByName(newName);
    if (exist.valid()) {
        pushObjHandle(L, ctx, exist, newName);
        return 1;
    }
    Object o = *src;
    o.name = newName;
    o.uid.clear();
    o.parent = GvtId<Object>{};
    GvtId<Object> id = sc->Gvtcier_Scene_RegisterObject(std::move(o));
    Object* no = sc->Gvtcier_Scene_GetObject(id);
    if (ctx->phys && no) ctx->phys->Gvtcier_Physics_AddObject(*no, id);
    pushObjHandle(L, ctx, id, newName);
    return 1;
}

// 脚本名：删除_子物体(实体)
// 参数：父实体句柄。返回：无。
// 调用：Scene::Gvtcier_Scene_DetachChildren，解除全部子物体并保留它们的世界变换。
// 内部又是按名字重新查一次 ID 的，所以实体刚被改名的话可能查不到。
static int g_cr_detachChildren(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    if (!o || !ctx->scene) return 0;
    ctx->scene->Gvtcier_Scene_DetachChildren(ctx->scene->Gvtcier_Scene_FindByName(o->name.c_str()));
    return 0;
}

// 脚本名：获取_世界包围盒(实体)
// 参数：实体句柄。返回：6 个数，最小角 xyz 与最大角 xyz；实体无效返回 空。
// 算法：取网格半长乘各自缩放轴的绝对值，再以实体位置为中心加减。
// 注意这里用的是实体位置的轴对齐盒，忽略了旋转，旋转后包围盒会偏小；
// 没有网格的实体按 0.5 立方体算。
static int g_cr_worldBounds(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    if (!o || !ctx->scene) { gvts_pushnil(L); return 1; }
    Vec3 h(0.5f, 0.5f, 0.5f);
    if (ctx->scene->Gvtcier_Scene_GetMesh(o->mesh)) h = ctx->scene->Gvtcier_Scene_MeshHalfExtents(o->mesh);
    float hx = std::fabs(h.x * o->scale.x);
    float hy = std::fabs(h.y * o->scale.y);
    float hz = std::fabs(h.z * o->scale.z);
    gvts_pushnumber(L, o->pos.x - hx);
    gvts_pushnumber(L, o->pos.y - hy);
    gvts_pushnumber(L, o->pos.z - hz);
    gvts_pushnumber(L, o->pos.x + hx);
    gvts_pushnumber(L, o->pos.y + hy);
    gvts_pushnumber(L, o->pos.z + hz);
    return 6;
}

// 脚本名：获取_实体按ID(唯一ID)
// 参数：实体的 uid 字符串。返回：实体句柄，找不到返回 空。
// 与 获取_实体 的区别：名字可能重复，uid 是场景内唯一的，适合存档与网络同步。
static int g_cr_getObjectByUid(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* uid = gvtsG_checkstring(L, 1);
    if (!ctx->scene) { gvts_pushnil(L); return 1; }
    Scene* sc = ctx->scene;
    GvtId<Object> id = sc->Gvtcier_Scene_FindByUid(uid);
    Object* o = sc->Gvtcier_Scene_GetObject(id);
    if (o) {
        pushObjHandle(L, ctx, id, o->name.c_str());
        return 1;
    }
    gvts_pushnil(L);
    return 1;
}

// 脚本名：获取_实体(名字)
// 参数：实体名，精确匹配（同名时取场景里第一个找到的）。返回：实体句柄，找不到返回 空。
static int g_cr_getObject(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx->scene) { gvts_pushnil(L); return 1; }
    Scene* sc = ctx->scene;
    GvtId<Object> id = sc->Gvtcier_Scene_FindByName(name);
    Object* o = sc->Gvtcier_Scene_GetObject(id);
    if (o) {
        pushObjHandle(L, ctx, id, o->name.c_str());
        return 1;
    }
    gvts_pushnil(L);
    return 1;
}

// 形状名到内置网格的映射。中英文都认，未知形状一律回落到方块，
// 这样脚本写错形状名也不会得到空实体，只是长得不像预期。
static GvtId<Mesh> meshFromShape(Scene* sc, const char* shape) {
    if (!shape || !shape[0]) return sc->cubeMesh();
    if (strcmp(shape, u8"方块") == 0 || strcmp(shape, "cube") == 0) return sc->cubeMesh();
    if (strcmp(shape, u8"球") == 0 || strcmp(shape, "sphere") == 0) return sc->sphereMesh();
    if (strcmp(shape, u8"地面") == 0 || strcmp(shape, "plane") == 0) return sc->planeMesh();
    if (strcmp(shape, u8"圆盘") == 0 || strcmp(shape, "disc") == 0) return sc->planeCircleMesh();
    if (strcmp(shape, u8"三角") == 0 || strcmp(shape, "tri") == 0) return sc->planeTriMesh();
    if (strcmp(shape, u8"锥体") == 0 || strcmp(shape, "cone") == 0) return sc->coneMesh();
    if (strcmp(shape, u8"圆柱") == 0 || strcmp(shape, "cylinder") == 0) return sc->cylinderMesh();
    if (strcmp(shape, u8"圆环") == 0 || strcmp(shape, "torus") == 0) return sc->torusMesh();
    return sc->cubeMesh();
}

// 碰撞体形状名到 ColliderType 的映射。未知或空都当作无碰撞，
// 所以脚本里拼错"盒"会得到一个不参与碰撞的实体。
static ColliderType colliderFromName(const char* c) {
    if (!c || !c[0]) return ColliderType::None;
    if (strcmp(c, u8"盒") == 0 || strcmp(c, "box") == 0) return ColliderType::Box;
    if (strcmp(c, u8"球") == 0 || strcmp(c, "sphere") == 0) return ColliderType::Sphere;
    if (strcmp(c, u8"胶囊") == 0 || strcmp(c, "capsule") == 0) return ColliderType::Capsule;
    if (strcmp(c, u8"圆柱") == 0 || strcmp(c, "cylinder") == 0) return ColliderType::Cylinder;
    if (strcmp(c, u8"随形") == 0 || strcmp(c, "hull") == 0) return ColliderType::ConvexHull;
    if (strcmp(c, u8"凸包") == 0 || strcmp(c, "convex") == 0) return ColliderType::ConvexHull;
    if (strcmp(c, u8"网格") == 0 || strcmp(c, "mesh") == 0) return ColliderType::Mesh;
    if (strcmp(c, "none") == 0 || strcmp(c, u8"无") == 0) return ColliderType::None;
    return ColliderType::None;
}

// 碰撞体类型的反向映射，读属性"碰撞形状"时用。
// 注意 Mesh 类型没有对应分支，会落到 default 返回"无"，读出来跟实际不符。
static const char* colliderToName(ColliderType t) {
    switch (t) {
        case ColliderType::Box: return u8"盒";
        case ColliderType::Sphere: return u8"球";
        case ColliderType::Capsule: return u8"胶囊";
        case ColliderType::Cylinder: return u8"圆柱";
        case ColliderType::ConvexHull: return u8"凸包";
        default: return u8"无";
    }
}

// 新建_实体 时按形状自动配碰撞体的规则：
//   球 -> 球，胶囊 -> 胶囊，圆柱/圆盘 -> 圆柱，锥体/圆环 -> 凸包，其余（方块/地面/三角）-> 盒。
// 圆盘用圆柱近似是因为 Jolt 没有独立的圆盘形状，薄圆柱最接近。
static ColliderType colliderForShape(const char* shape) {
    if (!shape || !shape[0]) return ColliderType::Box;
    if (strcmp(shape, u8"球") == 0 || strcmp(shape, "sphere") == 0) return ColliderType::Sphere;
    if (strcmp(shape, u8"胶囊") == 0 || strcmp(shape, "capsule") == 0) return ColliderType::Capsule;
    if (strcmp(shape, u8"圆柱") == 0 || strcmp(shape, "cylinder") == 0) return ColliderType::Cylinder;
    if (strcmp(shape, u8"锥体") == 0 || strcmp(shape, "cone") == 0) return ColliderType::ConvexHull;
    if (strcmp(shape, u8"圆盘") == 0 || strcmp(shape, "disc") == 0) return ColliderType::Cylinder;
    if (strcmp(shape, u8"圆环") == 0 || strcmp(shape, "torus") == 0) return ColliderType::ConvexHull;
    if (strcmp(shape, u8"方块") == 0 || strcmp(shape, "cube") == 0) return ColliderType::Box;
    if (strcmp(shape, u8"地面") == 0 || strcmp(shape, "plane") == 0) return ColliderType::Box;
    if (strcmp(shape, u8"三角") == 0 || strcmp(shape, "tri") == 0) return ColliderType::Box;
    return ColliderType::Box;
}

// 脚本名：新建_实体(名字[, 形状[, x[, y[, z]]]])
// 参数：名字；形状默认"方块"；坐标默认 0。
// 返回：实体句柄；同名的已存在时直接返回已有的（幂等，脚本重复执行不会叠出多个）。
// 建出来的实体：几何类型、白色纹理、按形状自动配碰撞体；"地面"形状的缩放默认 10x1x10，
// 这样新手写一行代码就能得到一个能踩的地板。
// 注册之后立刻加进物理世界。
static int g_entityCreate(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) { gvts_pushnil(L); return 1; }
    const char* name = gvtsG_checkstring(L, 1);
    const char* shape = gvtsG_optstring(L, 2, u8"方块");
    float x = (float)gvtsG_optnumber(L, 3, 0.0);
    float y = (float)gvtsG_optnumber(L, 4, 0.0);
    float z = (float)gvtsG_optnumber(L, 5, 0.0);
    Scene* sc = ctx->scene;
    GvtId<Object> found = sc->Gvtcier_Scene_FindByName(name);
    if (found.valid()) {
        pushObjHandle(L, ctx, found, name);
        return 1;
    }
    Object o;
    o.name = name;
    o.kind = ObjKind::Geometry;
    o.mesh = meshFromShape(sc, shape);
    o.tex = sc->whiteTex();
    o.pos = Vec3(x, y, z);
    o.collider = colliderForShape(shape);
    if (strcmp(shape, u8"地面") == 0) o.scale = Vec3(10, 1, 10);
    GvtId<Object> id = sc->Gvtcier_Scene_RegisterObject(std::move(o));
    Object* no = sc->Gvtcier_Scene_GetObject(id);
    if (ctx->phys && no) ctx->phys->Gvtcier_Physics_AddObject(*no, id);
    pushObjHandle(L, ctx, id, name);
    return 1;
}

// 脚本名：删除_实体(名字[, 立刻生效])
// 参数：实体名；第二参为真时当场删，缺省（假）等到帧末统一删。
// 返回：1 个布尔，表示是否找到了要删的实体（不是"是否已删掉"）。
// 延迟删除是为了避免在遍历场景的过程中销毁实体导致迭代器失效；
// 立刻生效那条路会顺手跑一次 FlushRemoves 并同步物理世界。
static int g_entityDestroy(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) { gvts_pushboolean(L, 0); return 1; }
    const char* name = gvtsG_checkstring(L, 1);
    bool now = gvts_toboolean(L, 2) != 0;
    Scene* sc = ctx->scene;
    GvtId<Object> id = sc->Gvtcier_Scene_FindByName(name);
    if (id.valid()) {
        sc->queueRemove(id);
        if (now && ctx->phys) sc->Gvtcier_Scene_FlushRemoves(ctx->phys);
        gvts_pushboolean(L, 1);
        return 1;
    }
    gvts_pushboolean(L, 0);
    return 1;
}

// 脚本名：查询_实体(名称片段)
// 参数：关键词，按子串模糊匹配实体名。返回：实体句柄数组（可能为空表）。
// 适合做"所有 enemy 开头的敌人都动一下"这类批量操作。
static int g_entityByName(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx->scene) { gvts_newtable(L); return 1; }
    Scene* sc = ctx->scene;
    gvts_newtable(L);
    int n = 0;
    sc->ForEachObject([&](const Object& o, GvtId<Object> id) {
        if (o.name.find(name) != std::string::npos) {
            pushObjHandle(L, ctx, id, o.name.c_str());
            gvts_rawseti(L, -2, ++n);
        }
    });
    return 1;
}

// 脚本名：查询_标签实体(标签)
// 参数：标签名，必须完全相等（与 查询_实体 的子串匹配不同）。返回：实体句柄数组。
static int g_cr_getObjectsByTag(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* tag = gvtsG_checkstring(L, 1);
    if (!ctx->scene) { gvts_newtable(L); return 1; }
    Scene* sc = ctx->scene;
    gvts_newtable(L);
    int n = 0;
    sc->ForEachObject([&](const Object& o, GvtId<Object> id) {
        if (o.tag == tag) {
            pushObjHandle(L, ctx, id, o.name.c_str());
            gvts_rawseti(L, -2, ++n);
        }
    });
    return 1;
}

// 下面是 获取_属性 / 设置_属性 的实现前方声明。
// 拆成四个函数是因为属性分为数值型与字符串型两类，读写各一个，主函数按属性名分派。

static int propGetNum(Object* o, const char* key, gvts_State* L);

static bool propGetStr(Object* o, const char* key, gvts_State* L);

static bool propSetNum(Object* o, const char* key, gvts_State* L, int base);

static bool propSetStr(Object* o, const char* key, const char* v);

// 取实体参数：优先当实体句柄，取不到再把参数当字符串，按名字或 uid 去场景里查。
// 这样 获取_属性("主角", "位置X") 和 获取_属性(主角句柄, "位置X") 两种写法都能用。
static Object* argObjectOrKey(gvts_State* L, int idx, GvtsEngineCtx* ctx) {
    Object* o = argObject(L, idx);
    if (o || !ctx || !ctx->scene) return o;
    if (!gvts_isstring(L, idx)) return nullptr;
    const char* key = gvts_tostring(L, idx);
    if (!key || !key[0]) return nullptr;
    GvtId<Object> found = ctx->scene->Gvtcier_Scene_FindByName(key);
    if (!found.valid()) found = ctx->scene->Gvtcier_Scene_FindByUid(key);
    return ctx->scene->Gvtcier_Scene_GetObject(found);
}

// 脚本名：获取_属性(实体, 属性名)
// 参数：实体句柄或实体名；属性名（中文，如 "位置X"、"颜色"、"纹理"）。
// 返回：属性值。数值属性可能返回 1 个值也可能返回 3 个值（"位置"/"旋转"这类向量属性），
//       字符串属性返回字符串，未知属性打一行日志并返回 空。
// 分派顺序：先特判"父物体"（要返回句柄），再试数值型，最后试字符串型。
static int g_propGet(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObjectOrKey(L, 1, ctx);
    const char* key = gvtsG_checkstring(L, 2);
    if (!o) { gvts_pushnil(L); return 1; }
    if (strcmp(key, "父物体") == 0) {
        const Object* po = (ctx && ctx->scene) ? ctx->scene->Gvtcier_Scene_GetObject(o->parent) : nullptr;
        if (po) {
            pushObjHandle(L, ctx, o->parent, po->name.c_str());
            return 1;
        }
        gvts_pushnil(L);
        return 1;
    }
    int n = propGetNum(o, key, L);
    if (n > 0) return n;
    if (propGetStr(o, key, L)) return 1;
    printf("[gvts] 获取_属性: 未知属性 '%s'（实体 '%s'）\n", key, o->name.c_str());
    gvts_pushnil(L);
    return 1;
}

// 判断这个属性改动是否会影响物理体的形状/质量，从而需要重建物理体。
// 碰撞、物理开关、刚体/可推动/运动学、质量、摩擦以及任意缩放轴都算。
// "缩放"整体也要算进去，否则整体缩放后碰撞体还是旧尺寸。
static bool physTouchedKey(const char* key) {
    return strcmp(key, "碰撞") == 0 || strcmp(key, "物理") == 0 || strcmp(key, "刚体") == 0 || strcmp(key, "可推动") == 0 ||
           strcmp(key, "运动学") == 0 || strcmp(key, "质量") == 0 || strcmp(key, "摩擦") == 0 ||
           strcmp(key, "缩放") == 0 ||
           strcmp(key, "缩放X") == 0 || strcmp(key, "缩放Y") == 0 || strcmp(key, "缩放Z") == 0;
}

// 脚本名：设置_属性(实体, 属性名, 值)
// 参数：实体句柄或实体名；属性名；值（数值/布尔/字符串，向量属性要一次传三个分量）。
// 返回：无（成功失败都不返回，失败时打日志）。
// 特判了三个属性，因为它们要动引擎的其他系统而不只是改字段：
//   父物体：走 Gvtcier_Scene_SetParent，传 空 表示断开；
//   纹理：按内置资源名换贴图；
//   六面纹理：一次传六个内置贴图名，凑齐才作为立方体贴图加载。
// 其余属性先试数值，失败再试字符串；若改到了物理相关属性，会先移除再加回物理体来重建形状。
// 速度/角速度/平台速度这类还要额外同步给物理系统，否则物体会无视脚本设置直接弹回去。
static int g_propSet(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObjectOrKey(L, 1, ctx);
    const char* key = gvtsG_checkstring(L, 2);
    if (!o) return 0;
    if (strcmp(key, "父物体") == 0) {
        if (!ctx || !ctx->scene) return 0;
        GvtId<Object> child = ctx->scene->Gvtcier_Scene_IdOf(o);
        if (!child.valid()) return 0;
        Object* np = gvts_isnil(L, 3) ? nullptr : argObject(L, 3);
        GvtId<Object> parent = np ? ctx->scene->Gvtcier_Scene_IdOf(np) : GvtId<Object>{};
        ctx->scene->Gvtcier_Scene_SetParent(child, parent);
        return 0;
    }
    if (strcmp(key, "纹理") == 0) {
        if (!ctx || !ctx->scene) return 0;
        const char* res = gvtsG_optstring(L, 3, nullptr);
        if (!res || !res[0]) return 0;
        GvtId<Texture> t = ctx->scene->Gvtcier_Scene_BuiltinTexture(res);
        if (t.valid()) { o->tex = t; o->texName = res; }
        return 0;
    }
    if (strcmp(key, "六面纹理") == 0) {
        if (!ctx || !ctx->scene) return 0;
        const char* names[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        const char* res[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
        int n = 0;
        for (int i = 0; i < 6; ++i) {
            const char* v = gvtsG_optstring(L, 3 + i, nullptr);
            if (!v || !v[0]) break;
            const char* rn = ctx->scene->Gvtcier_Scene_BuiltinResName(v);
            if (!rn) break;
            names[n] = v;
            res[n] = rn;
            ++n;
        }
        if (n == 6) {
            GvtId<Texture> t = ctx->scene->Gvtcier_Scene_LoadPngArrayRes(res, 6);
            if (t.valid()) { o->tex6 = t; o->tex6Name.clear(); for (int i = 0; i < 6; ++i) { o->tex6Name += names[i]; if (i < 5) o->tex6Name += ','; } }
        }
        return 0;
    }
    bool touched = physTouchedKey(key);
    bool ok = propSetNum(o, key, L, 3);
    if (!ok && gvts_isstring(L, 3)) ok = propSetStr(o, key, gvts_tostring(L, 3));
    if (!ok && strcmp(key, "父物体") != 0) {
        printf("[gvts] 设置_属性: 未知属性 '%s'（实体 '%s'）\n", key, o->name.c_str());
        return 0;
    }
    if (ok && (strcmp(key, "平台速度") == 0 || strcmp(key, "平台角速度") == 0 ||
               strcmp(key, "平台枢轴") == 0)) {
        if (ctx && ctx->phys && ctx->scene) {
            GvtId<Object> id = ctx->scene->Gvtcier_Scene_IdOf(o);
            if (id.valid()) ctx->phys->Gvtcier_Physics_SetKinematic(id, o->kinVel, o->kinAngVel, o->kinPivot);
        }
        return 0;
    }
    if (ok && strcmp(key, "速度") == 0 && ctx && ctx->phys && ctx->scene) {
        GvtId<Object> id = ctx->scene->Gvtcier_Scene_IdOf(o);
        if (id.valid()) ctx->phys->Gvtcier_Physics_SetObjectVelocity(id, o->vel);
        return 0;
    }
    if (ok && (strcmp(key, "角速度") == 0 || strcmp(key, "角速度X") == 0 ||
               strcmp(key, "角速度Y") == 0 || strcmp(key, "角速度Z") == 0) &&
        ctx && ctx->phys && ctx->scene) {
        GvtId<Object> id = ctx->scene->Gvtcier_Scene_IdOf(o);
        if (id.valid()) ctx->phys->Gvtcier_Physics_SetObjectAngularVelocity(id, o->angVel);
        return 0;
    }
    if (touched && ctx && ctx->phys && ctx->scene) {
        GvtId<Object> id = ctx->scene->Gvtcier_Scene_IdOf(o);
        if (id.valid()) {
            ctx->phys->Gvtcier_Physics_RemoveObject(id);
            ctx->phys->Gvtcier_Physics_AddObject(*o, id);
        }
    }
    return 0;
}

// 数值/布尔属性的读取分派。返回压栈的值个数，0 表示不认识这个属性名。
// 全部按中文属性名做字符串比较，是一长串 if，看起来笨但直白，改起来不容易出错。
// 返回 true 的地方等价于返回 1（C++ 的 true 转 int 就是 1）。
static int propGetNum(Object* o, const char* key, gvts_State* L) {
    if (strcmp(key, "颜色R") == 0) { gvts_pushnumber(L, o->albedoTint.x); return 1; }
    if (strcmp(key, "颜色G") == 0) { gvts_pushnumber(L, o->albedoTint.y); return 1; }
    if (strcmp(key, "颜色B") == 0) { gvts_pushnumber(L, o->albedoTint.z); return 1; }
    if (strcmp(key, "位置") == 0) {
        gvts_pushnumber(L, o->pos.x); gvts_pushnumber(L, o->pos.y); gvts_pushnumber(L, o->pos.z);
        return 3;
    }
    if (strcmp(key, "位置X") == 0) { gvts_pushnumber(L, o->pos.x); return 1; }
    if (strcmp(key, "位置Y") == 0) { gvts_pushnumber(L, o->pos.y); return 1; }
    if (strcmp(key, "位置Z") == 0) { gvts_pushnumber(L, o->pos.z); return 1; }
    if (strcmp(key, "旋转") == 0) {
        gvts_pushnumber(L, o->rotDeg.x); gvts_pushnumber(L, o->rotDeg.y); gvts_pushnumber(L, o->rotDeg.z);
        return 3;
    }
    if (strcmp(key, "局部位置") == 0) {
        gvts_pushnumber(L, o->localPos.x); gvts_pushnumber(L, o->localPos.y); gvts_pushnumber(L, o->localPos.z);
        return 3;
    }
    if (strcmp(key, "局部旋转") == 0) {
        gvts_pushnumber(L, o->localRotDeg.x); gvts_pushnumber(L, o->localRotDeg.y); gvts_pushnumber(L, o->localRotDeg.z);
        return 3;
    }
    if (strcmp(key, "局部缩放") == 0) {
        gvts_pushnumber(L, o->localScale.x); gvts_pushnumber(L, o->localScale.y); gvts_pushnumber(L, o->localScale.z);
        return 3;
    }
    if (strcmp(key, "缩放") == 0) {
        gvts_pushnumber(L, o->scale.x); gvts_pushnumber(L, o->scale.y); gvts_pushnumber(L, o->scale.z);
        return 3;
    }
    if (strcmp(key, "速度") == 0) {
        gvts_pushnumber(L, o->vel.x); gvts_pushnumber(L, o->vel.y); gvts_pushnumber(L, o->vel.z);
        return 3;
    }
    if (strcmp(key, "平台速度") == 0) {
        gvts_pushnumber(L, o->kinVel.x); gvts_pushnumber(L, o->kinVel.y); gvts_pushnumber(L, o->kinVel.z);
        return 3;
    }
    if (strcmp(key, "平台角速度") == 0) {
        gvts_pushnumber(L, o->kinAngVel.x); gvts_pushnumber(L, o->kinAngVel.y); gvts_pushnumber(L, o->kinAngVel.z);
        return 3;
    }
    if (strcmp(key, "平台枢轴") == 0) {
        gvts_pushnumber(L, o->kinPivot.x); gvts_pushnumber(L, o->kinPivot.y); gvts_pushnumber(L, o->kinPivot.z);
        return 3;
    }
    if (strcmp(key, "角速度") == 0) {
        gvts_pushnumber(L, o->angVel.x); gvts_pushnumber(L, o->angVel.y); gvts_pushnumber(L, o->angVel.z);
        return 3;
    }
    if (strcmp(key, "光照颜色") == 0) {
        gvts_pushnumber(L, o->lightColor.x); gvts_pushnumber(L, o->lightColor.y); gvts_pushnumber(L, o->lightColor.z);
        return 3;
    }
    if (strcmp(key, "激光起点") == 0) {
        gvts_pushnumber(L, o->laserA.x); gvts_pushnumber(L, o->laserA.y); gvts_pushnumber(L, o->laserA.z);
        return 3;
    }
    if (strcmp(key, "激光终点") == 0) {
        gvts_pushnumber(L, o->laserB.x); gvts_pushnumber(L, o->laserB.y); gvts_pushnumber(L, o->laserB.z);
        return 3;
    }
    if (strcmp(key, "质量") == 0) { gvts_pushnumber(L, o->mass); return 1; }
    if (strcmp(key, "物理") == 0) { gvts_pushboolean(L, o->physics); return 1; }
    if (strcmp(key, "摩擦") == 0) { gvts_pushnumber(L, o->friction); return 1; }
    if (strcmp(key, "光照强度") == 0) { gvts_pushnumber(L, o->lightIntensity); return 1; }
    if (strcmp(key, "光照范围") == 0) { gvts_pushnumber(L, o->lightRange); return 1; }
    if (strcmp(key, "视场") == 0) { gvts_pushnumber(L, o->fov); return 1; }
    if (strcmp(key, "音量") == 0) { gvts_pushnumber(L, o->volume); return 1; }
    if (strcmp(key, "音高") == 0) { gvts_pushnumber(L, o->pitch); return true; }
    if (strcmp(key, "移动速度") == 0) { gvts_pushnumber(L, o->moveSpeed); return true; }
    if (strcmp(key, "跳跃高度") == 0) { gvts_pushnumber(L, o->jumpHeight); return true; }
    if (strcmp(key, "可见") == 0) { gvts_pushboolean(L, o->visible); return true; }
    if (strcmp(key, "投影") == 0) { gvts_pushboolean(L, o->castShadow); return true; }
    if (strcmp(key, "刚体") == 0) { gvts_pushboolean(L, o->rigidBody); return true; }
    if (strcmp(key, "可推动") == 0) { gvts_pushboolean(L, o->pushable); return true; }
    if (strcmp(key, "运动学") == 0) { gvts_pushboolean(L, o->kinematic); return true; }
    if (strcmp(key, "静态") == 0) { gvts_pushboolean(L, o->staticXform); return true; }
    if (strcmp(key, "缩放X") == 0) { gvts_pushnumber(L, o->scale.x); return true; }
    if (strcmp(key, "缩放Y") == 0) { gvts_pushnumber(L, o->scale.y); return true; }
    if (strcmp(key, "缩放Z") == 0) { gvts_pushnumber(L, o->scale.z); return true; }
    if (strcmp(key, "高光") == 0) { gvts_pushnumber(L, o->shininess); return true; }
    if (strcmp(key, "材质") == 0) { gvts_pushinteger(L, o->material); return true; }
    if (strcmp(key, "光照投影") == 0) { gvts_pushboolean(L, o->lightShadow); return true; }
    if (strcmp(key, "聚光角") == 0) { gvts_pushnumber(L, o->spotAngle); return true; }
    if (strcmp(key, "近裁面") == 0) { gvts_pushnumber(L, o->nearPlane); return true; }
    if (strcmp(key, "远裁面") == 0) { gvts_pushnumber(L, o->farPlane); return true; }
    if (strcmp(key, "声音范围") == 0) { gvts_pushnumber(L, o->soundRange); return true; }
    if (strcmp(key, "空间音频") == 0) { gvts_pushboolean(L, o->spatial); return true; }
    if (strcmp(key, "距离衰减") == 0) { gvts_pushboolean(L, o->attenuation); return true; }
    if (strcmp(key, "文本尺寸") == 0) { gvts_pushnumber(L, o->textSize); return true; }
    if (strcmp(key, "特效类型") == 0) { gvts_pushinteger(L, o->effectType); return true; }
    if (strcmp(key, "特效参数0") == 0) { gvts_pushnumber(L, o->effectParam0); return true; }
    if (strcmp(key, "特效参数1") == 0) { gvts_pushnumber(L, o->effectParam1); return true; }
    if (strcmp(key, "特效方向") == 0) { gvts_pushnumber(L, o->effectDir); return true; }
    if (strcmp(key, "特效自发光") == 0) { gvts_pushnumber(L, o->effectEmissive); return true; }
    if (strcmp(key, "自动播放") == 0) { gvts_pushboolean(L, o->autoPlay); return true; }
    if (strcmp(key, "循环") == 0) { gvts_pushboolean(L, o->loop); return true; }
    if (strcmp(key, "激光宽度") == 0) { gvts_pushnumber(L, o->laserWidth); return true; }
    if (strcmp(key, "纹理平铺") == 0) { gvts_pushnumber(L, o->uvScale); return true; }
    if (strcmp(key, "透明度") == 0) { gvts_pushnumber(L, o->alpha); return true; }
    return false;
}

// 字符串属性的读取分派，返回是否认识这个属性名。名称/ID/纹理/标签/碰撞形状/贴图/文本/模型/音频/特效/脚本。
static bool propGetStr(Object* o, const char* key, gvts_State* L) {
    if (strcmp(key, "名称") == 0) { gvts_pushstring(L, o->name.c_str()); return true; }
    if (strcmp(key, "ID") == 0) { gvts_pushstring(L, o->uid.c_str()); return true; }
    if (strcmp(key, "纹理") == 0) { gvts_pushstring(L, o->texName.c_str()); return true; }
    if (strcmp(key, "标签") == 0) { gvts_pushstring(L, o->tag.c_str()); return true; }
    if (strcmp(key, "碰撞形状") == 0) { gvts_pushstring(L, colliderToName(o->collider)); return true; }
    if (strcmp(key, "贴图") == 0) { gvts_pushstring(L, o->imageName.c_str()); return true; }
    if (strcmp(key, "文本") == 0) { gvts_pushstring(L, o->text.c_str()); return true; }
    if (strcmp(key, "模型") == 0) { gvts_pushstring(L, o->modelName.c_str()); return true; }
    if (strcmp(key, "音频") == 0) { gvts_pushstring(L, o->audioPath.c_str()); return true; }
    if (strcmp(key, "特效") == 0) { gvts_pushstring(L, o->effectAsset.c_str()); return true; }
    if (strcmp(key, "脚本") == 0) { gvts_pushstring(L, o->scriptPath.c_str()); return true; }
    return false;
}

// 数值/布尔属性的写入分派，base 是值在栈上的起始位置（向量属性要用 base、base+1、base+2）。
// 返回是否认识这个属性名。
// 几个要点：
//   "刚体"/"可推动"/"运动学" 在实体总物理开关关闭时会拒绝打开，只打日志不改值，
//     因为物理体都没建出来，单独开这些标志没有意义；
//   改位置/旋转/缩放后会调 MarkXformDirty，让下一帧重算变换；
//   "局部位置/局部旋转/局部缩放" 会额外直接置 xformDirty，比 MarkXformDirty 更早生效；
//   "透明度" 会被夹到 0~1，并顺带更新 transparent 标志。
static bool propSetNum(Object* o, const char* key, gvts_State* L, int base) {
    if (strcmp(key, "碰撞") == 0) {
        o->collider = colliderFromName(gvtsG_optstring(L, base, u8"盒"));
        return true;
    }
    if (strcmp(key, "可见") == 0) { o->visible = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "投影") == 0) { o->castShadow = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "刚体") == 0) {
        bool v = gvts_toboolean(L, base) != 0;
        if (v && !o->physics && !o->rigidBody) {
            printf("[gvts] 设置_属性: 实体 '%s' 物理已关闭, 拒绝激活刚体\n", o->name.c_str());
            return true;
        }
        o->rigidBody = v;
        return true;
    }
    if (strcmp(key, "可推动") == 0) {
        bool v = gvts_toboolean(L, base) != 0;
        if (v && !o->physics && !o->pushable) {
            printf("[gvts] 设置_属性: 实体 '%s' 物理已关闭, 拒绝激活可推动\n", o->name.c_str());
            return true;
        }
        o->pushable = v;
        return true;
    }
    if (strcmp(key, "运动学") == 0) {
        bool v = gvts_toboolean(L, base) != 0;
        if (v && !o->physics && !o->kinematic) {
            printf("[gvts] 设置_属性: 实体 '%s' 物理已关闭, 拒绝激活运动学\n", o->name.c_str());
            return true;
        }
        o->kinematic = v;
        return true;
    }
    if (strcmp(key, "颜色") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->albedoTint = Vec3((float)gvtsG_checknumber(L, base),
                             (float)gvtsG_checknumber(L, base + 1),
                             (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "颜色R") == 0) { if (!gvts_isnumber(L, base)) return false; o->albedoTint.x = (float)gvts_tonumber(L, base); return true; }
    if (strcmp(key, "颜色G") == 0) { if (!gvts_isnumber(L, base)) return false; o->albedoTint.y = (float)gvts_tonumber(L, base); return true; }
    if (strcmp(key, "颜色B") == 0) { if (!gvts_isnumber(L, base)) return false; o->albedoTint.z = (float)gvts_tonumber(L, base); return true; }
    if (strcmp(key, "光照颜色R") == 0) { if (!gvts_isnumber(L, base)) return false; o->lightColor.x = (float)gvts_tonumber(L, base); return true; }
    if (strcmp(key, "光照颜色G") == 0) { if (!gvts_isnumber(L, base)) return false; o->lightColor.y = (float)gvts_tonumber(L, base); return true; }
    if (strcmp(key, "光照颜色B") == 0) { if (!gvts_isnumber(L, base)) return false; o->lightColor.z = (float)gvts_tonumber(L, base); return true; }
    if (strcmp(key, "缩放") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->scale = Vec3((float)gvtsG_checknumber(L, base),
                        (float)gvtsG_checknumber(L, base + 1),
                        (float)gvtsG_checknumber(L, base + 2));
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "位置") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->pos = Vec3((float)gvtsG_checknumber(L, base),
                      (float)gvtsG_checknumber(L, base + 1),
                      (float)gvtsG_checknumber(L, base + 2));
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "位置X") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->pos.x = (float)gvtsG_checknumber(L, base);
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "位置Y") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->pos.y = (float)gvtsG_checknumber(L, base);
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "位置Z") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->pos.z = (float)gvtsG_checknumber(L, base);
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "旋转") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->rotDeg = Vec3((float)gvtsG_checknumber(L, base),
                         (float)gvtsG_checknumber(L, base + 1),
                         (float)gvtsG_checknumber(L, base + 2));
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "局部位置") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->localPos = Vec3((float)gvtsG_checknumber(L, base),
                           (float)gvtsG_checknumber(L, base + 1),
                           (float)gvtsG_checknumber(L, base + 2));
        o->xformDirty = true;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "局部旋转") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->localRotDeg = Vec3((float)gvtsG_checknumber(L, base),
                              (float)gvtsG_checknumber(L, base + 1),
                              (float)gvtsG_checknumber(L, base + 2));
        o->xformDirty = true;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "局部缩放") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->localScale = Vec3((float)gvtsG_checknumber(L, base),
                             (float)gvtsG_checknumber(L, base + 1),
                             (float)gvtsG_checknumber(L, base + 2));
        o->xformDirty = true;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "速度") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->vel = Vec3((float)gvtsG_checknumber(L, base),
                      (float)gvtsG_checknumber(L, base + 1),
                      (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "平台速度") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->kinVel = Vec3((float)gvtsG_checknumber(L, base),
                         (float)gvtsG_checknumber(L, base + 1),
                         (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "平台角速度") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->kinAngVel = Vec3((float)gvtsG_checknumber(L, base),
                            (float)gvtsG_checknumber(L, base + 1),
                            (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "平台枢轴") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->kinPivot = Vec3((float)gvtsG_checknumber(L, base),
                           (float)gvtsG_checknumber(L, base + 1),
                           (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "角速度") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->angVel = Vec3((float)gvtsG_checknumber(L, base),
                         (float)gvtsG_checknumber(L, base + 1),
                         (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "角速度X") == 0) { o->angVel.x = (float)gvtsG_checknumber(L, base); return true; }
    if (strcmp(key, "角速度Y") == 0) { o->angVel.y = (float)gvtsG_checknumber(L, base); return true; }
    if (strcmp(key, "角速度Z") == 0) { o->angVel.z = (float)gvtsG_checknumber(L, base); return true; }
    if (strcmp(key, "光照颜色") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->lightColor = Vec3((float)gvtsG_checknumber(L, base),
                             (float)gvtsG_checknumber(L, base + 1),
                             (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "激光起点") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->laserA = Vec3((float)gvtsG_checknumber(L, base),
                         (float)gvtsG_checknumber(L, base + 1),
                         (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "激光终点") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->laserB = Vec3((float)gvtsG_checknumber(L, base),
                         (float)gvtsG_checknumber(L, base + 1),
                         (float)gvtsG_checknumber(L, base + 2));
        return true;
    }
    if (strcmp(key, "缩放X") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->scale.x = (float)gvtsG_checknumber(L, base);
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "缩放Y") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->scale.y = (float)gvtsG_checknumber(L, base);
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "缩放Z") == 0) {
        if (!gvts_isnumber(L, base)) return false;
        o->scale.z = (float)gvtsG_checknumber(L, base);
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "物理") == 0 && (gvts_isboolean(L, base) || gvts_isnumber(L, base))) {
        o->physics = gvts_isboolean(L, base) ? (gvts_toboolean(L, base) != 0) : (gvts_tonumber(L, base) != 0);
        return true;
    }
    if (!gvts_isnumber(L, base) && !gvts_isboolean(L, base)) return false;
    float v = (float)gvts_tonumber(L, base);
    if (strcmp(key, "质量") == 0) { o->mass = v; return true; }
    if (strcmp(key, "摩擦") == 0) { o->friction = v; return true; }
    if (strcmp(key, "光照强度") == 0) { o->lightIntensity = v; return true; }
    if (strcmp(key, "光照范围") == 0) { o->lightRange = v; return true; }
    if (strcmp(key, "视场") == 0) { o->fov = v; return true; }
    if (strcmp(key, "音量") == 0) { o->volume = v; return true; }
    if (strcmp(key, "音高") == 0) { o->pitch = v; return true; }
    if (strcmp(key, "移动速度") == 0) { o->moveSpeed = v; return true; }
    if (strcmp(key, "跳跃高度") == 0) { o->jumpHeight = v; return true; }
    if (strcmp(key, "高光") == 0) { o->shininess = v; return true; }
    if (strcmp(key, "材质") == 0) { o->material = (int)v; return true; }
    if (strcmp(key, "光照投影") == 0) { o->lightShadow = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "聚光角") == 0) { o->spotAngle = v; return true; }
    if (strcmp(key, "近裁面") == 0) { o->nearPlane = v; return true; }
    if (strcmp(key, "远裁面") == 0) { o->farPlane = v; return true; }
    if (strcmp(key, "声音范围") == 0) { o->soundRange = v; return true; }
    if (strcmp(key, "空间音频") == 0) { o->spatial = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "距离衰减") == 0) { o->attenuation = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "文本尺寸") == 0) { o->textSize = v; return true; }
    if (strcmp(key, "特效类型") == 0) { o->effectType = (int)v; return true; }
    if (strcmp(key, "特效参数0") == 0) { o->effectParam0 = v; return true; }
    if (strcmp(key, "特效参数1") == 0) { o->effectParam1 = v; return true; }
    if (strcmp(key, "特效方向") == 0) { o->effectDir = v; return true; }
    if (strcmp(key, "特效自发光") == 0) { o->effectEmissive = v; return true; }
    if (strcmp(key, "自动播放") == 0) { o->autoPlay = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "循环") == 0) { o->loop = gvts_toboolean(L, base) != 0; return true; }
    if (strcmp(key, "激光宽度") == 0) { o->laserWidth = v; return true; }
    if (strcmp(key, "纹理平铺") == 0) { o->uvScale = v; return true; }
    if (strcmp(key, "透明度") == 0) {
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        o->alpha = v;
        o->transparent = v < 1.0f;
        return true;
    }
    if (strcmp(key, "静态") == 0) {
        o->staticXform = gvts_toboolean(L, base) != 0;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    return false;
}

// 字符串属性的写入分派，返回是否认识这个属性名。
// 与读取对应的那批属性一致，但少了"ID"和"碰撞形状"：这两个是只读的派生字段。
// 注意"名称"可以直接改，改完不会被场景的名字索引自动更新，后续按旧名查可能查不到。
static bool propSetStr(Object* o, const char* key, const char* v) {
    if (strcmp(key, "名称") == 0) { o->name = v; return true; }
    if (strcmp(key, "标签") == 0) { o->tag = v; return true; }
    if (strcmp(key, "贴图") == 0) { o->imageName = v; return true; }
    if (strcmp(key, "文本") == 0) { o->text = v; return true; }
    if (strcmp(key, "模型") == 0) { o->modelName = v; return true; }
    if (strcmp(key, "音频") == 0) { o->audioPath = v; return true; }
    if (strcmp(key, "特效") == 0) { o->effectAsset = v; return true; }
    if (strcmp(key, "脚本") == 0) { o->scriptPath = v; return true; }
    return false;
}

// 下面两个是给引擎内部 C++ 代码用的属性读写接口（不在脚本里注册），
// 与脚本版 propGetNum/propSetNum 的差别是：
//   1) 不碰 gvts 栈，直接往 out / 从 v 取值；
//   2) 向量属性用 comp 指定分量（0=x 1=y 2=z），而不是一次读三个；
//   3) 支持的范围小一些，只有渐变和动画关心那些常用数值属性。
// 时间线的 执行_渐变、动画状态机的属性轨道都靠这两个函数驱动。

// 按属性名 + 分量读出数值，返回是否支持该属性。
bool Gvtcier_Prop_GetNum(Object* o, const char* key, int comp, double* out) {
    if (!o || !key || !out) return false;
    if (comp < 0 || comp > 2) comp = 0;
    if (strcmp(key, "颜色R") == 0) { *out = o->albedoTint.x; return true; }
    if (strcmp(key, "颜色G") == 0) { *out = o->albedoTint.y; return true; }
    if (strcmp(key, "颜色B") == 0) { *out = o->albedoTint.z; return true; }
    if (strcmp(key, "位置X") == 0) { *out = o->pos.x; return true; }
    if (strcmp(key, "位置Y") == 0) { *out = o->pos.y; return true; }
    if (strcmp(key, "位置Z") == 0) { *out = o->pos.z; return true; }
    if (strcmp(key, "位置") == 0) {
        *out = comp == 0 ? o->pos.x : (comp == 1 ? o->pos.y : o->pos.z);
        return true;
    }
    if (strcmp(key, "旋转") == 0) {
        *out = comp == 0 ? o->rotDeg.x : (comp == 1 ? o->rotDeg.y : o->rotDeg.z);
        return true;
    }
    if (strcmp(key, "缩放X") == 0) { *out = o->scale.x; return true; }
    if (strcmp(key, "缩放Y") == 0) { *out = o->scale.y; return true; }
    if (strcmp(key, "缩放Z") == 0) { *out = o->scale.z; return true; }
    if (strcmp(key, "缩放") == 0) {
        *out = comp == 0 ? o->scale.x : (comp == 1 ? o->scale.y : o->scale.z);
        return true;
    }
    if (strcmp(key, "光照强度") == 0) { *out = o->lightIntensity; return true; }
    if (strcmp(key, "光照范围") == 0) { *out = o->lightRange; return true; }
    if (strcmp(key, "视场") == 0) { *out = o->fov; return true; }
    if (strcmp(key, "音量") == 0) { *out = o->volume; return true; }
    if (strcmp(key, "音高") == 0) { *out = o->pitch; return true; }
    if (strcmp(key, "移动速度") == 0) { *out = o->moveSpeed; return true; }
    if (strcmp(key, "跳跃高度") == 0) { *out = o->jumpHeight; return true; }
    if (strcmp(key, "高光") == 0) { *out = o->shininess; return true; }
    if (strcmp(key, "聚光角") == 0) { *out = o->spotAngle; return true; }
    if (strcmp(key, "近裁面") == 0) { *out = o->nearPlane; return true; }
    if (strcmp(key, "远裁面") == 0) { *out = o->farPlane; return true; }
    if (strcmp(key, "声音范围") == 0) { *out = o->soundRange; return true; }
    if (strcmp(key, "文本尺寸") == 0) { *out = o->textSize; return true; }
    if (strcmp(key, "纹理平铺") == 0) { *out = o->uvScale; return true; }
    if (strcmp(key, "质量") == 0) { *out = o->mass; return true; }
    if (strcmp(key, "摩擦") == 0) { *out = o->friction; return true; }
    if (strcmp(key, "激光宽度") == 0) { *out = o->laserWidth; return true; }
    if (strcmp(key, "透明度") == 0) { *out = o->alpha; return true; }
    return false;
}

// 按属性名 + 分量写入数值，返回是否支持该属性。
// 动到位置/旋转/缩放时同样会标记变换脏，透明度同样会被夹到 0~1 并更新 transparent。
bool Gvtcier_Prop_SetNum(Object* o, const char* key, int comp, double v) {
    if (!o || !key) return false;
    if (comp < 0 || comp > 2) comp = 0;
    float f = (float)v;
    if (strcmp(key, "颜色R") == 0) { o->albedoTint.x = f; return true; }
    if (strcmp(key, "颜色G") == 0) { o->albedoTint.y = f; return true; }
    if (strcmp(key, "颜色B") == 0) { o->albedoTint.z = f; return true; }
    if (strcmp(key, "位置X") == 0) { o->pos.x = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "位置Y") == 0) { o->pos.y = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "位置Z") == 0) { o->pos.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "位置") == 0) {
        if (comp == 0) o->pos.x = f; else if (comp == 1) o->pos.y = f; else o->pos.z = f;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "旋转") == 0) {
        if (comp == 0) o->rotDeg.x = f; else if (comp == 1) o->rotDeg.y = f; else o->rotDeg.z = f;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "缩放X") == 0) { o->scale.x = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放Y") == 0) { o->scale.y = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放Z") == 0) { o->scale.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放") == 0) {
        if (comp == 0) o->scale.x = f; else if (comp == 1) o->scale.y = f; else o->scale.z = f;
        o->Gvtcier_Object_MarkXformDirty();
        return true;
    }
    if (strcmp(key, "光照强度") == 0) { o->lightIntensity = f; return true; }
    if (strcmp(key, "光照范围") == 0) { o->lightRange = f; return true; }
    if (strcmp(key, "视场") == 0) { o->fov = f; return true; }
    if (strcmp(key, "音量") == 0) { o->volume = f; return true; }
    if (strcmp(key, "音高") == 0) { o->pitch = f; return true; }
    if (strcmp(key, "移动速度") == 0) { o->moveSpeed = f; return true; }
    if (strcmp(key, "跳跃高度") == 0) { o->jumpHeight = f; return true; }
    if (strcmp(key, "高光") == 0) { o->shininess = f; return true; }
    if (strcmp(key, "聚光角") == 0) { o->spotAngle = f; return true; }
    if (strcmp(key, "近裁面") == 0) { o->nearPlane = f; return true; }
    if (strcmp(key, "远裁面") == 0) { o->farPlane = f; return true; }
    if (strcmp(key, "声音范围") == 0) { o->soundRange = f; return true; }
    if (strcmp(key, "文本尺寸") == 0) { o->textSize = f; return true; }
    if (strcmp(key, "纹理平铺") == 0) { o->uvScale = f; return true; }
    if (strcmp(key, "质量") == 0) { o->mass = f; return true; }
    if (strcmp(key, "摩擦") == 0) { o->friction = f; return true; }
    if (strcmp(key, "激光宽度") == 0) { o->laserWidth = f; return true; }
    if (strcmp(key, "透明度") == 0) {
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        o->alpha = f;
        o->transparent = f < 1.0f;
        return true;
    }
    return false;
}

// 脚本名：查询_最近实体(标签)
// 参数：标签名。返回：距玩家最近的该标签实体句柄，没有则返回 空。
// 距离按实体位置到玩家位置的直线距离算，没考虑遮挡与导航路径。
// 需要 ctx->player 有效，没有玩家实体时直接返回 空。
static int g_entNearest(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* tag = gvtsG_checkstring(L, 1);
    if (!ctx->scene || !ctx->player) { gvts_pushnil(L); return 1; }
    Scene* sc = ctx->scene;
    GvtId<Object> best;
    float bestD = 0.0f;
    sc->ForEachObject([&](const Object& o, GvtId<Object> id) {
        if (o.tag != tag) return;
        float d = (o.pos - ctx->player->pos).length();
        if (!best.valid() || d < bestD) { best = id; bestD = d; }
    });
    if (!best.valid()) { gvts_pushnil(L); return 1; }
    const Object* bo = sc->Gvtcier_Scene_GetObject(best);
    pushObjHandle(L, ctx, best, bo ? bo->name.c_str() : "");
    return 1;
}

// 脚本名：检测_在范围内(实体, x, y, z, 半径)
// 参数：实体句柄；球心世界坐标；半径。返回：1 个布尔。
// 纯数学判断，不走物理查询，所以速度很快，但也无视碰撞体形状（只看实体原点）。
static int g_entWithin(gvts_State* L) {
    Object* o = argObject(L, 1);
    float x = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float z = (float)gvtsG_checknumber(L, 4);
    float r = (float)gvtsG_checknumber(L, 5);
    bool ok = o && (o->pos - Vec3(x, y, z)).length() <= r;
    gvts_pushboolean(L, ok);
    return 1;
}

// 脚本名：获取_子物体(实体)
// 参数：父实体句柄。返回：子实体句柄数组（可能为空表）。
// 判断依据是子实体的 parent 字段等于该实体的 ID，只找直接子级，不递归。
static int g_entChildren(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    Object* o = argObject(L, 1);
    gvts_newtable(L);
    if (!o || !ctx || !ctx->scene) return 1;
    GvtId<Object> id = ctx->scene->Gvtcier_Scene_IdOf(o);
    if (!id.valid()) return 1;
    int n = 0;
    ctx->scene->ForEachObject([&](const Object& c, GvtId<Object> cid) {
        if (c.parent != id) return;
        pushObjHandle(L, ctx, cid, c.name.c_str());
        gvts_rawseti(L, -2, ++n);
    });
    return 1;
}

// 脚本名：获取_实体距离(实体A, 实体B)
// 参数：两个实体句柄。返回：1 个数（世界坐标下的直线距离），任一无效返回 空。
static int g_entDistanceTo(gvts_State* L) {
    Object* a = argObject(L, 1);
    Object* b = argObject(L, 2);
    if (!a || !b) { gvts_pushnil(L); return 1; }
    gvts_pushnumber(L, (a->pos - b->pos).length());
    return 1;
}

// 脚本名：获取_实体列表()
// 参数：无。返回：场景全部实体的句柄数组。
// 遍历顺序就是场景内部的对象存储顺序，不保证与创建顺序完全一致。
static int g_entityAll(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->scene) { gvts_newtable(L); return 1; }
    Scene* sc = ctx->scene;
    gvts_newtable(L);
    int n = 0;
    sc->ForEachObject([&](const Object& o, GvtId<Object> id) {
        pushObjHandle(L, ctx, id, o.name.c_str());
        gvts_rawseti(L, -2, ++n);
    });
    return 1;
}

// 脚本名：加载_地图(gmap路径)
// 参数：.gmap 文件路径。
// 返回：1 个布尔，成功为真；文件没包含、或场景里已经有地图时返回 真（视为"已经在用"），
//       路径非法/文件未包含时返回 假。
// 两条前置门槛：
//   1) 路径必须已经通过脚本顶部的 包含 <...> 声明过（gvtsFileIncluded），
//      这是为了让资源打包工具能静态扫出所有依赖的文件；
//   2) 如果场景里已有"可见且带碰撞的几何体"，认为地图已经加载过，直接返回成功，不重复加载。
// 真正加载时会先清物理体、清物体、再 LoadGmap，最后把新物体重新加进物理世界。
static int g_scene_loadgmap(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* gmapPath = gvtsG_checkstring(L, 1);
    if (!ctx || !ctx->scene || !gmapPath || !gmapPath[0]) {
        gvts_pushboolean(L, 0);
        return 1;
    }
    if (!gvtsFileIncluded(gmapPath)) {
        std::string msg = std::string(u8"文件未包含，请先在脚本写 包含 <") + gmapPath + u8">";
        printf("[gvts] %s\n", msg.c_str());
        gvts_pushboolean(L, 0);
        return 1;
    }
    Scene* sc = ctx->scene;
    bool alreadyLoaded = false;
    sc->ForEachObject([&](const Object& o, GvtId<Object>) {
        if (alreadyLoaded) return;
        if (o.kind == ObjKind::Geometry && o.visible && o.collider != ColliderType::None) alreadyLoaded = true;
    });
    if (alreadyLoaded) {
        gvts_pushboolean(L, 1);
        return 1;
    }
    std::vector<GvtId<Object>> oldIds = sc->Gvtcier_Scene_ObjectIds();
    if (ctx->phys) ctx->phys->Gvtcier_Physics_RemoveObjects(oldIds);
    sc->Gvtcier_Scene_ClearObjects();
    bool ok = sc->Gvtcier_Scene_LoadGmap(gmapPath);
    if (ok && ctx->phys) {
        sc->ForEachObject([&](const Object& o, GvtId<Object> id) {
            ctx->phys->Gvtcier_Physics_AddObject(o, id);
        });
    }
    gvts_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// 脚本名：执行_切换(函数名或 .gmap)
// 参数：一个字符串。
// 返回：1 个布尔，成功为真。
// 两种模式：
//   1) 传的是已存在的全局函数名 -> 直接调用它，用于"切场景"这种脚本自己实现的逻辑；
//   2) 以 .gmap 结尾 -> 清空当前场景并加载新地图（与 加载_地图 的清理流程一致）。
// 都不匹配时返回 假，所以脚本里可以写成 if not 执行_切换(...) then ... end。
static int g_scene_switch(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* v = gvtsG_checkstring(L, 1);
    if (!ctx || !v || !v[0]) { gvts_pushboolean(L, 0); return 1; }
    std::string val(v);

    gvts_getglobal(L, val.c_str());
    if (gvts_isfunction(L, -1)) {
        if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
            printf("[gvts] 执行_切换函数失败: %s\n", gvts_tostring(L, -1));
            gvts_pop(L, 1);
            gvts_pushboolean(L, 0);
            return 1;
        }
        gvts_pushboolean(L, 1);
        return 1;
    }
    gvts_pop(L, 1);

    bool gmap = val.size() > 5 && val.compare(val.size() - 5, 5, ".gmap") == 0;
    if (!gmap || !ctx->scene) { gvts_pushboolean(L, 0); return 1; }

    Scene* sc = ctx->scene;
    std::vector<GvtId<Object>> rm = sc->Gvtcier_Scene_ObjectIds();
    if (ctx->phys) ctx->phys->Gvtcier_Physics_RemoveObjects(rm);
    sc->Gvtcier_Scene_ClearObjects();
    bool ok = sc->Gvtcier_Scene_LoadGmap(val.c_str());
    if (ok && ctx->phys) {
        sc->ForEachObject([&](const Object& o, GvtId<Object> id) {
            ctx->phys->Gvtcier_Physics_AddObject(o, id);
        });
    }
    if (!ok) printf("[gvts] 执行_切换地图失败: %s\n", val.c_str());
    gvts_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// 注册实体与场景相关的全部能力，分"实体""空间""场景"三个分组。
// 这里只补一个总览，逐条说明请对照上面每个函数前的注释：
//   实体组：快照/新建/删除/按名/按ID/模糊查询/按标签/全列表/最近/子物体/副本/解绑子物体/
//           世界包围盒/刷新/读写属性；
//   空间组：实体间距离、点半径内检测；
//   场景组：加载地图、执行切换、清空、场景体检。
void Gvtcier_Bind_Scene(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"实体", u8"获取_实体快照", u8"一次读取实体全部真实字段，返回字段表；不存在返回 nil", g_entity_snapshot);
    regCap(L, ctx, u8"实体", u8"新建_实体", u8"按名称与形状创建实体并在物理世界注册", g_entityCreate);
    regCap(L, ctx, u8"实体", u8"删除_实体", u8"按名称删除实体，第二参数为真时立刻生效，缺省帧末生效", g_entityDestroy);
    regCap(L, ctx, u8"实体", u8"获取_实体", u8"按精确名称取实体句柄", g_cr_getObject);
    regCap(L, ctx, u8"实体", u8"获取_实体按ID", u8"按唯一 ID 取实体句柄，不存在返回 空", g_cr_getObjectByUid);
    regCap(L, ctx, u8"实体", u8"查询_实体", u8"按名称子串模糊匹配，返回列表", g_entityByName);
    regCap(L, ctx, u8"实体", u8"查询_标签实体", u8"按标签取实体列表", g_cr_getObjectsByTag);
    regCap(L, ctx, u8"实体", u8"获取_实体列表", u8"返回场景全部实体列表", g_entityAll);
    regCap(L, ctx, u8"实体", u8"查询_最近实体", u8"取距玩家最近的指定标签实体", g_entNearest);
    regCap(L, ctx, u8"实体", u8"获取_子物体", u8"返回指定实体的全部子物体列表", g_entChildren);
    regCap(L, ctx, u8"实体", u8"创建_实体副本", u8"按现有实体复制出新实体，返回新句柄", g_cr_cloneEntity);
    regCap(L, ctx, u8"实体", u8"删除_子物体", u8"解除该实体的全部子物体，子物体保留世界变换", g_cr_detachChildren);
    regCap(L, ctx, u8"实体", u8"获取_世界包围盒", u8"返回世界空间最小角与最大角 6 个值", g_cr_worldBounds);
    regCap(L, ctx, u8"实体", u8"执行_刷新实体", u8"强制立刻重算层级变换，不等下帧", g_cr_refresh);
    regCap(L, ctx, u8"实体", u8"获取_属性", u8"按属性名读取实体属性", g_propGet);
    regCap(L, ctx, u8"实体", u8"设置_属性", u8"按属性名写入实体属性", g_propSet);
    regCap(L, ctx, u8"空间", u8"获取_实体距离", u8"两实体间距离", g_entDistanceTo);
    regCap(L, ctx, u8"空间", u8"检测_在范围内", u8"实体是否落在以给定点为心的球内", g_entWithin);
    regCap(L, ctx, u8"场景", u8"加载_地图", u8"加载 .gmap 地图文件（网格与三角网格碰撞）：gmap路径，返回是否成功", g_scene_loadgmap);
    regCap(L, ctx, u8"场景", u8"执行_切换", u8"切换执行目标：值可为函数名（调用）或 .gmap 文件（卸载当前场景并加载新地图）", g_scene_switch);
    regCap(L, ctx, u8"场景", u8"删除_场景内容", u8"删除当前场景全部物体", g_scene_clear);
    regCap(L, ctx, u8"场景", u8"检测_场景问题", u8"遍历全场景返回问题列表：重名/位置NaN/缩放非法/父级异常/模型缺网格；空表即通过", g_scene_validate);
}
