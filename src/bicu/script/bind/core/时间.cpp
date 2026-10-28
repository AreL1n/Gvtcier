#include "Gvtcier.h"

// 脚本绑定：时间。提供等待、计时器、帧间隔等与时间相关的脚本接口。
// 所有时间单位都是秒（浮点），与引擎内部一致。

static GvtsEngineCtx* timeCtx(gvts_State* L) {
    return (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
}

// 让脚本协程等待若干秒后继续。注意这不阻塞主线程，是脚本层的调度。
static int g_time_wait(gvts_State* L) {
    double secs = gvtsG_optnumber(L, 1, 0.0);
    if (secs < 0.0) secs = 0.0;
    if (gvts_pushthread(L) == 1) { gvts_pop(L, 1); return 0; }
    gvts_pop(L, 1);
    gvts_pushnumber(L, secs);
    return gvts_yield(L, 1);
}

static int g_time_exec(gvts_State* L) {
    GvtsEngineCtx* ctx = timeCtx(L);
    if (!gvts_isfunction(L, 1)) return 0;
    gvts_State* co = gvts_newthread(L);
    gvts_pushvalue(L, 1);
    gvts_xmove(L, co, 1);
    int coRef = gvtsG_ref(L, GVTS_REGISTRYINDEX);
    gvts_pop(L, 1);
    int nres = 0;
    int st = gvts_resume(co, L, 0, &nres);
    if (st == GVTS_YIELD) {
        double wait = nres > 0 ? gvts_tonumber(L, -1) : 0.0;
        gvts_pop(L, nres);
        if (ctx) ctx->waitCos.push_back({ (void*)co, wait, coRef });
        return 0;
    }
    if (st != GVTS_OK) {
        printf("[gvts] 执行_命令 失败: %s\n", gvts_tostring(L, -1));
        gvts_pop(L, 1);
    }
    gvts_pop(L, nres);
    gvtsG_unref(L, GVTS_REGISTRYINDEX, coRef);
    return 0;
}

static int g_time_cancel(gvts_State* L) {
    GvtsEngineCtx* ctx = timeCtx(L);
    if (!ctx) return 0;
    ctx->tweens.clear();
    return 0;
}

static const char* kTweenMeta = "GvtcierTweenMeta";

struct TweenObj {
    GvtsEngineCtx* ctx;
    GvtId<Object> obj;
    char key[64];
    int comp;
    double to;
    double dur;
};

static int g_tween_in(gvts_State* L) {
    TweenObj* t = (TweenObj*)gvtsG_checkudata(L, 1, kTweenMeta);
    double d = gvtsG_optnumber(L, 2, 0.3);
    t->dur = d > 0.0 ? d : 0.0;
    gvts_pushvalue(L, 1);
    return 1;
}

static int g_tween_to(gvts_State* L) {
    TweenObj* t = (TweenObj*)gvtsG_checkudata(L, 1, kTweenMeta);
    t->to = gvtsG_optnumber(L, 2, 0.0);
    if (t->ctx && t->ctx->scene) {
        Object* o = t->ctx->scene->Gvtcier_Scene_GetObject(t->obj);
        double from = 0.0;
        if (o && Gvtcier_Prop_GetNum(o, t->key, t->comp, &from)) {
            GvtsTween tw;
            tw.obj = t->obj;
            tw.key = t->key;
            tw.propId = 0;
            tw.comp = t->comp;
            tw.from = from;
            tw.to = t->to;
            tw.dur = t->dur;
            tw.cur = 0.0;
            tw.targetKind = 1;
            t->ctx->tweens.push_back(tw);
        }
    }
    gvts_pushvalue(L, 1);
    return 1;
}

static int g_tween_gc(gvts_State* L) {
    (void)L;
    return 0;
}

static int g_time_fade(gvts_State* L) {
    GvtsEngineCtx* ctx = timeCtx(L);
    Object* o = argObject(L, 1);
    if (!gvts_isstring(L, 2)) { gvts_pushnil(L); return 1; }
    const char* key = gvts_tostring(L, 2);
    int comp = (int)gvtsG_optnumber(L, 3, 0.0);
    TweenObj* t = (TweenObj*)gvts_newuserdata(L, sizeof(TweenObj));
    memset(t, 0, sizeof(TweenObj));
    t->ctx = ctx;
    t->obj = o ? ctx->scene->Gvtcier_Scene_IdOf(o) : GvtId<Object>{};
    strncpy(t->key, key ? key : "", sizeof(t->key) - 1);
    t->comp = comp;
    t->dur = 0.3;
    gvtsG_getmetatable(L, kTweenMeta);
    gvts_setmetatable(L, -2);
    return 1;
}

void Gvtcier_Bind_Time(gvts_State* L, GvtsEngineCtx* ctx) {
    gvtsG_newmetatable(L, kTweenMeta);
    gvts_pushcfunction(L, g_tween_gc);
    gvts_setfield(L, -2, "__gc");
    gvts_newtable(L);
    gvts_pushcfunction(L, g_tween_in);
    gvts_setfield(L, -2, "用时");
    gvts_pushcfunction(L, g_tween_to);
    gvts_setfield(L, -2, "到");
    gvts_setfield(L, -2, "__index");
    gvts_pop(L, 1);

    regCap(L, ctx, u8"时间", u8"执行_等待", u8"等待指定秒数后继续执行后续代码，不冻结引擎：执行_等待(秒)", g_time_wait);
    regCap(L, ctx, u8"时间", u8"执行_命令", u8"立即执行传入的函数", g_time_exec);
    regCap(L, ctx, u8"时间", u8"删除_等待", u8"清空当前全部渐变任务", g_time_cancel);
    regCap(L, ctx, u8"渲染", u8"执行_渐变", u8"对对象数值属性做渐变：执行_渐变(对象, \"属性\"):用时(秒):到(值)", g_time_fade);
}

void Gvtcier_Time_Tick(GvtsEngineCtx* ctx, float dt) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->tweens.size();) {
        GvtsTween& tw = ctx->tweens[i];
        Object* to = ctx->scene ? ctx->scene->Gvtcier_Scene_GetObject(tw.obj) : nullptr;
        if (!to) { ctx->tweens.erase(ctx->tweens.begin() + i); continue; }
        if (tw.dur <= 0.0) {
            if (!tw.key.empty()) Gvtcier_Prop_SetNum(to, tw.key.c_str(), tw.comp, tw.to);
            ctx->tweens.erase(ctx->tweens.begin() + i);
            continue;
        }
        tw.cur += (double)dt;
        double t = tw.cur / tw.dur;
        if (t > 1.0) t = 1.0;
        if (t < 0.0) t = 0.0;
        double v = tw.from + (tw.to - tw.from) * t;
        if (!tw.key.empty()) Gvtcier_Prop_SetNum(to, tw.key.c_str(), tw.comp, v);
        if (t >= 1.0) {
            ctx->tweens.erase(ctx->tweens.begin() + i);
        } else {
            ++i;
        }
    }
}

void Gvtcier_Time_Clear(GvtsEngineCtx* ctx) {
    if (!ctx) return;
    ctx->tweens.clear();
    ctx->timers.clear();
    ctx->waitCos.clear();
}