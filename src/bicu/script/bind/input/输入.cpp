#include "Gvtcier.h"

// 脚本绑定：输入。脚本查询键盘、鼠标、手柄状态。
// 键值参数用 Windows 虚拟键码（如 0x20 空格、0x41 是 A 键）。
// 当输入被冻结（inputFrozen，例如菜单打开）或查询的是菜单键时，一律返回假，
// 避免玩家按 ESC 开菜单的同时游戏脚本也收到这个按键。

static int g_cr_keyDown(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int vk = (int)gvtsG_checkinteger(L, 1);
    if (ctx->inputFrozen || vk == Gvtcier_Settings().menuKey) { gvts_pushboolean(L, 0); return 1; }
    if (ctx->win) { gvts_pushboolean(L, ctx->win->isKeyDown(vk)); return 1; }
    gvts_pushboolean(L, 0);
    return 1;
}

static int g_cr_scroll(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->inputFrozen) { gvts_pushinteger(L, 0); return 1; }
    gvts_pushinteger(L, ctx->win ? ctx->win->Gvtcier_Window_ConsumeScroll() : 0);
    return 1;
}

static bool g_keyPrev[256] = {};

static bool g_keyInit = false;

static int g_cr_keyPressed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int vk = (int)gvtsG_checkinteger(L, 1);
    if (vk < 0 || vk > 255 || !ctx->win) { gvts_pushboolean(L, 0); return 1; }
    if (vk == Gvtcier_Settings().menuKey) { gvts_pushboolean(L, 0); return 1; }
    bool down = ctx->win->isKeyDown(vk);
    if (!g_keyInit) {
        for (int i = 0; i < 256; ++i) g_keyPrev[i] = ctx->win->isKeyDown(i);
        g_keyInit = true;
    }
    bool edge = down && !g_keyPrev[vk];
    g_keyPrev[vk] = down;
    if (ctx->inputFrozen) { gvts_pushboolean(L, 0); return 1; }
    gvts_pushboolean(L, edge);
    return 1;
}

static int g_cr_keySync(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->win) return 0;
    for (int i = 0; i < 256; ++i) g_keyPrev[i] = ctx->win->isKeyDown(i);
    g_keyInit = true;
    return 0;
}

static bool actionDown(GvtsEngineCtx* ctx, int action) {
    if (!ctx->win || ctx->inputFrozen) return false;
    int vk = Gvtcier_Settings().keyBindings[action];
    if (vk == 0) return false;
    return ctx->win->isKeyDown(vk);
}

static int g_cr_actionDown(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int action = (int)gvtsG_checkinteger(L, 1);
    if (action < 0 || action >= ACTION_COUNT) { gvts_pushboolean(L, 0); return 1; }
    gvts_pushboolean(L, actionDown(ctx, action) ? 1 : 0);
    return 1;
}

static int g_cr_actionKey(gvts_State* L) {
    int action = (int)gvtsG_checkinteger(L, 1);
    if (action < 0 || action >= ACTION_COUNT) { gvts_pushinteger(L, 0); return 1; }
    gvts_pushinteger(L, Gvtcier_Settings().keyBindings[action]);
    return 1;
}

static int g_cr_actionName(gvts_State* L) {
    int action = (int)gvtsG_checkinteger(L, 1);
    gvts_pushstring(L, Gvtcier_Settings_ActionName(action));
    return 1;
}

static int g_cr_keyName(gvts_State* L) {
    int vk = (int)gvtsG_checkinteger(L, 1);
    std::string n = Gvtcier_Settings_KeyName(vk);
    gvts_pushstring(L, n.c_str());
    return 1;
}

static int g_cr_setKey(gvts_State* L) {
    int action = (int)gvtsG_checkinteger(L, 1);
    int vk = (int)gvtsG_checkinteger(L, 2);
    if (action < 0 || action >= ACTION_COUNT) return 0;
    Gvtcier_Settings().keyBindings[action] = vk;
    Gvtcier_Settings().keyPreset = PRESET_CUSTOM;
    return 0;
}

static int g_cr_setMenuKey(gvts_State* L) {
    int vk = (int)gvtsG_checkinteger(L, 1);
    Gvtcier_Settings().menuKey = vk;
    return 0;
}

static int g_cr_menuKey(gvts_State* L) {
    gvts_pushinteger(L, Gvtcier_Settings().menuKey);
    return 1;
}

static int g_cr_registerAction(gvts_State* L) {
    const char* id = gvtsG_checkstring(L, 1);
    const char* name = gvtsG_checkstring(L, 2);
    int vk = (int)gvtsG_checkinteger(L, 3);
    gvts_pushinteger(L, Gvtcier_Settings_RegisterAction(id, name, vk));
    return 1;
}

static int g_cr_actionValue(gvts_State* L) {
    const char* id = gvtsG_checkstring(L, 1);
    int idx = Gvtcier_Settings_FindAction(id);
    gvts_pushinteger(L, idx >= 0 ? Gvtcier_Settings_ActionVkAt(idx) : 0);
    return 1;
}

static int g_cr_setActionValue(gvts_State* L) {
    const char* id = gvtsG_checkstring(L, 1);
    int vk = (int)gvtsG_checkinteger(L, 2);
    int idx = Gvtcier_Settings_FindAction(id);
    if (idx >= 0) Gvtcier_Settings().actions[idx].vk = vk;
    return 0;
}

static bool g_actPrev[256] = {};

static int g_cr_actionKeyDown(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* id = gvtsG_checkstring(L, 1);
    int idx = Gvtcier_Settings_FindAction(id);
    int vk = idx >= 0 ? Gvtcier_Settings_ActionVkAt(idx) : 0;
    if (vk <= 0 || vk > 255 || !ctx->win || ctx->inputFrozen) { gvts_pushboolean(L, 0); return 1; }
    gvts_pushboolean(L, ctx->win->isKeyDown(vk) ? 1 : 0);
    return 1;
}

static int g_cr_actionKeyPressed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* id = gvtsG_checkstring(L, 1);
    int idx = Gvtcier_Settings_FindAction(id);
    int vk = idx >= 0 ? Gvtcier_Settings_ActionVkAt(idx) : 0;
    if (vk <= 0 || vk > 255 || !ctx->win) { gvts_pushboolean(L, 0); return 1; }
    bool down = ctx->win->isKeyDown(vk);
    bool edge = down && !g_actPrev[vk];
    g_actPrev[vk] = down;
    if (ctx->inputFrozen) { gvts_pushboolean(L, 0); return 1; }
    gvts_pushboolean(L, edge ? 1 : 0);
    return 1;
}

void Gvtcier_Bind_Input(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"输入", u8"检测_按键按下", u8"查询虚拟键是否按下", g_cr_keyDown);
    regCap(L, ctx, u8"输入", u8"检测_按键刚按下", u8"查询本帧是否刚按下（边沿触发）", g_cr_keyPressed);
    regCap(L, ctx, u8"输入", u8"执行_同步按键", u8"刷新按键边沿快照，每帧 tick 末尾调一次", g_cr_keySync);
    regCap(L, ctx, u8"输入", u8"获取_鼠标滚轮", u8"读取本帧滚轮增量，正上负下，读取后清零", g_cr_scroll);
    regCap(L, ctx, u8"输入", u8"检测_动作按下", u8"按动作编号查询动作绑定的键是否按下（动作编号见 设置界面 顺序）", g_cr_actionDown);
    regCap(L, ctx, u8"输入", u8"获取_动作按键", u8"读取某动作当前绑定的虚拟键码，0 表示未绑定", g_cr_actionKey);
    regCap(L, ctx, u8"输入", u8"获取_动作名称", u8"读取某动作的显示名", g_cr_actionName);
    regCap(L, ctx, u8"输入", u8"获取_按键名称", u8"把虚拟键码翻译为可读名称", g_cr_keyName);
    regCap(L, ctx, u8"输入", u8"设置_动作按键", u8"为动作重新绑定虚拟键码，会切到自定义预设", g_cr_setKey);
    regCap(L, ctx, u8"输入", u8"设置_菜单键", u8"设置引擎菜单开合键，该键不再透传给脚本", g_cr_setMenuKey);
    regCap(L, ctx, u8"输入", u8"获取_菜单键", u8"读取当前菜单键虚拟键码", g_cr_menuKey);
    regCap(L, ctx, u8"输入", u8"声明_按键", u8"声明按键动作：标识、设置中显示的名字、默认键值，返回动作序号", g_cr_registerAction);
    regCap(L, ctx, u8"输入", u8"获取_按键值", u8"按标识读取动作当前键值", g_cr_actionValue);
    regCap(L, ctx, u8"输入", u8"设置_按键值", u8"按标识修改动作键值", g_cr_setActionValue);
    regCap(L, ctx, u8"输入", u8"检测_动作键按下", u8"按标识查询动作键是否按下", g_cr_actionKeyDown);
    regCap(L, ctx, u8"输入", u8"检测_动作键刚按下", u8"按标识查询动作键是否本帧刚按下", g_cr_actionKeyPressed);
}
