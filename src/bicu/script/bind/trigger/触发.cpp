#include "Gvtcier.h"

// 脚本绑定：触发区域。脚本注册一块长方体区域，玩家进入/离开时回调脚本。
// 判定用轴对齐包围盒（中心 + 半尺寸），每帧对玩家位置做一次包含测试。

// 一个触发区域的定义。c 是中心，h 是半尺寸（不是全长）
struct TriggerRegion {
    Vec3 c;
    Vec3 h;
    bool prev;
    bool init;
};

static std::map<std::string, TriggerRegion> g_triggerRegions;

static bool triggerPlayerIn(GvtsEngineCtx* ctx, const TriggerRegion& t) {
    if (!ctx->player) return false;
    const Vec3& p = ctx->player->pos;
    return std::fabs(p.x - t.c.x) <= t.h.x &&
           std::fabs(p.y - t.c.y) <= t.h.y &&
           std::fabs(p.z - t.c.z) <= t.h.z;
}

static TriggerRegion* findTrigger(const char* name) {
    if (!name || !name[0]) return nullptr;
    auto it = g_triggerRegions.find(name);
    if (it == g_triggerRegions.end()) return nullptr;
    return &it->second;
}

static int g_trig_create(gvts_State* L) {
    const char* name = gvtsG_checkstring(L, 1);
    if (!name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    TriggerRegion t;
    t.c = Vec3((float)gvtsG_checknumber(L, 2), (float)gvtsG_checknumber(L, 3),
               (float)gvtsG_checknumber(L, 4));
    t.h = Vec3((float)gvtsG_checknumber(L, 5), (float)gvtsG_checknumber(L, 6),
               (float)gvtsG_checknumber(L, 7));
    t.prev = false;
    t.init = false;
    g_triggerRegions[name] = t;
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_trig_delete(gvts_State* L) {
    const char* name = gvtsG_checkstring(L, 1);
    gvts_pushboolean(L, g_triggerRegions.erase(name ? name : "") > 0);
    return 1;
}

static int g_trig_playerIn(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    TriggerRegion* t = findTrigger(gvtsG_checkstring(L, 1));
    if (!ctx || !t) { gvts_pushboolean(L, 0); return 1; }
    gvts_pushboolean(L, triggerPlayerIn(ctx, *t));
    return 1;
}

static int g_trig_entityIn(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    TriggerRegion* t = findTrigger(gvtsG_checkstring(L, 1));
    if (!ctx || !ctx->scene || !t) { gvts_pushboolean(L, 0); return 1; }
    GvtId<Object> id = ctx->scene->Gvtcier_Scene_FindByName(gvtsG_checkstring(L, 2));
    const Object* eo = ctx->scene->Gvtcier_Scene_GetObject(id);
    if (!eo) { gvts_pushboolean(L, 0); return 1; }
    const Vec3& p = eo->pos;
    gvts_pushboolean(L,
        std::fabs(p.x - t->c.x) <= t->h.x &&
        std::fabs(p.y - t->c.y) <= t->h.y &&
        std::fabs(p.z - t->c.z) <= t->h.z);
    return 1;
}

static int g_trig_enterEdge(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    TriggerRegion* t = findTrigger(gvtsG_checkstring(L, 1));
    if (!ctx || !t) { gvts_pushboolean(L, 0); return 1; }
    bool in = triggerPlayerIn(ctx, *t);
    if (!t->init) { t->init = true; t->prev = in; }
    bool edge = in && !t->prev;
    t->prev = in;
    gvts_pushboolean(L, edge);
    return 1;
}

static int g_trig_exitEdge(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    TriggerRegion* t = findTrigger(gvtsG_checkstring(L, 1));
    if (!ctx || !t) { gvts_pushboolean(L, 0); return 1; }
    bool in = triggerPlayerIn(ctx, *t);
    if (!t->init) { t->init = true; t->prev = in; }
    bool edge = !in && t->prev;
    t->prev = in;
    gvts_pushboolean(L, edge);
    return 1;
}

static int g_trig_onEnter(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    if (gvts_isfunction(L, 2)) {
        gvts_pushvalue(L, 2);
        ctx->trigEnterCb[name] = gvtsG_ref(L, GVTS_REGISTRYINDEX);
    } else {
        ctx->trigEnterCb.erase(name);
    }
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_trig_onExit(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    if (gvts_isfunction(L, 2)) {
        gvts_pushvalue(L, 2);
        ctx->trigExitCb[name] = gvtsG_ref(L, GVTS_REGISTRYINDEX);
    } else {
        ctx->trigExitCb.erase(name);
    }
    gvts_pushboolean(L, 1);
    return 1;
}

static void fireTriggerCb(gvts_State* L, std::map<std::string, int>& cbs,
                          const std::string& name) {
    auto it = cbs.find(name);
    if (it == cbs.end()) return;
    gvts_rawgeti(L, GVTS_REGISTRYINDEX, it->second);
    if (!gvts_isfunction(L, -1)) { gvts_pop(L, 1); return; }
    if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
        printf("[gvts] 触发区回调 %s 失败: %s\n", name.c_str(), gvts_tostring(L, -1));
        gvts_pop(L, 1);
    }
}

static int g_trig_ensure(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    if (findTrigger(name)) { gvts_pushboolean(L, 0); return 1; }
    if (!ctx->scene) { gvts_pushboolean(L, 0); return 1; }
    GvtId<Object> id = ctx->scene->Gvtcier_Scene_FindByName(name);
    const Object* o = ctx->scene->Gvtcier_Scene_GetObject(id);
    if (!o) { gvts_pushboolean(L, 0); return 1; }
    TriggerRegion t;
    t.c = o->pos;
    Vec3 h = o->Gvtcier_Object_MeshHalfExtents();
    t.h = Vec3(std::fabs(h.x) + 0.05f, std::fabs(h.y) + 0.05f, std::fabs(h.z) + 0.05f);
    t.prev = false;
    t.init = false;
    g_triggerRegions[name] = t;
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_trig_regBroad(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    if (gvts_isfunction(L, 2)) {
        gvts_pushvalue(L, 2);
        ctx->broadcasts[name].cbRef = gvtsG_ref(L, GVTS_REGISTRYINDEX);
    }
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_trig_sendBroad(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    const char* payload = gvtsG_optstring(L, 2, "");
    auto it = ctx->broadcasts.find(name);
    if (it == ctx->broadcasts.end()) { gvts_pushboolean(L, 0); return 1; }
    strncpy(it->second.payload, payload ? payload : "", sizeof(it->second.payload) - 1);
    it->second.pending = true;
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_trig_acceptBroad(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    if (!ctx || !name || !name[0]) { gvts_pushboolean(L, 0); return 1; }
    auto it = ctx->broadcasts.find(name);
    if (it == ctx->broadcasts.end() || !it->second.pending || !it->second.cbRef) {
        gvts_pushboolean(L, 0);
        return 1;
    }
    it->second.pending = false;
    gvts_rawgeti(L, GVTS_REGISTRYINDEX, it->second.cbRef);
    if (!gvts_isfunction(L, -1)) { gvts_pop(L, 1); gvts_pushboolean(L, 0); return 1; }
    if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
        printf("[gvts] 广播 %s 执行失败: %s\n", name, gvts_tostring(L, -1));
        gvts_pop(L, 1);
    }
    gvts_pushboolean(L, 1);
    return 1;
}

void Gvtcier_Trigger_Tick(gvts_State* L, GvtsEngineCtx* ctx) {
    if (!L || !ctx) return;
    if (!g_triggerRegions.empty()) {
        for (auto& kv : g_triggerRegions) {
            TriggerRegion& t = kv.second;
            bool in = triggerPlayerIn(ctx, t);
            if (!t.init) { t.init = true; t.prev = in; continue; }
            if (in && !t.prev) fireTriggerCb(L, ctx->trigEnterCb, kv.first);
            else if (!in && t.prev) fireTriggerCb(L, ctx->trigExitCb, kv.first);
            t.prev = in;
        }
    }
    if (ctx->scene) {
        for (auto& kv : ctx->trigEnterCb) {
            if (g_triggerRegions.count(kv.first)) continue;
            GvtId<Object> id = ctx->scene->Gvtcier_Scene_FindByName(kv.first.c_str());
            const Object* o = ctx->scene->Gvtcier_Scene_GetObject(id);
            if (!o) continue;
            TriggerRegion t;
            t.c = o->pos;
            Vec3 h = o->Gvtcier_Object_MeshHalfExtents();
            t.h = Vec3(std::fabs(h.x) + 0.05f, std::fabs(h.y) + 0.05f, std::fabs(h.z) + 0.05f);
            t.prev = false;
            t.init = false;
            g_triggerRegions[kv.first] = t;
        }
    }
    for (auto& kv : ctx->broadcasts) {
        if (!kv.second.pending || !kv.second.cbRef) continue;
        kv.second.pending = false;
        gvts_rawgeti(L, GVTS_REGISTRYINDEX, kv.second.cbRef);
        if (!gvts_isfunction(L, -1)) { gvts_pop(L, 1); continue; }
        if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
            printf("[gvts] 广播 %s 执行失败: %s\n", kv.first.c_str(), gvts_tostring(L, -1));
            gvts_pop(L, 1);
        }
    }
}

void Gvtcier_Bind_Trigger(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"交互", u8"创建_触发区", u8"创建 AABB 触发区：名, 中心X/Y/Z, 半长X/Y/Z", g_trig_create);
    regCap(L, ctx, u8"交互", u8"删除_触发区", u8"删除指定触发区：名", g_trig_delete);
    regCap(L, ctx, u8"交互", u8"检测_玩家在触发区", u8"查询玩家是否在触发区内：名", g_trig_playerIn);
    regCap(L, ctx, u8"交互", u8"检测_实体在触发区", u8"查询实体是否在触发区内：名, 实体名", g_trig_entityIn);
    regCap(L, ctx, u8"交互", u8"检测_进入触发区", u8"玩家本帧刚进入触发区（边沿）：名", g_trig_enterEdge);
    regCap(L, ctx, u8"交互", u8"检测_离开触发区", u8"玩家本帧刚离开触发区（边沿）：名", g_trig_exitEdge);
    regCap(L, ctx, u8"交互", u8"注册_进入触发区", u8"注册玩家进入触发区回调：名, 函数；传 nil 取消", g_trig_onEnter);
    regCap(L, ctx, u8"交互", u8"注册_离开触发区", u8"注册玩家离开触发区回调：名, 函数；传 nil 取消", g_trig_onExit);
    regCap(L, ctx, u8"交互", u8"注册_触发区", u8"按场景实体名自动建触发区：名", g_trig_ensure);
    regCap(L, ctx, u8"广播", u8"注册_广播", u8"注册广播事件：名, 函数", g_trig_regBroad);
    regCap(L, ctx, u8"广播", u8"发送_广播", u8"发出广播：名, 信息(可选)", g_trig_sendBroad);
    regCap(L, ctx, u8"广播", u8"接受_广播", u8"接受并执行待处理的广播：名，返回是否执行", g_trig_acceptBroad);
}
