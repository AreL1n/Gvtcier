#include "Gvtcier.h"

// 脚本绑定：UI 控件。脚本创建按钮、滑块、文本等交互控件，并响应点击。
// 控件绘制的画面归游戏 UI（canvasItems_），与引擎自带的菜单/设置界面分开。

#ifndef VK_LBUTTON
#define VK_LBUTTON 0x01
#endif

struct UiWidgetState {
    bool hovered = false;
    bool pressed = false;
    bool click = false;
    bool released = false;
};

static std::map<std::string, UiWidgetState> g_uiStates;
static std::map<std::string, bool> g_uiPrevFrame;
static std::map<std::string, bool> g_uiPrevDown;
static bool g_uiMouseWasDown = false;
static int g_uiCursorX = 0;
static int g_uiCursorY = 0;

static bool uiMouseDownNow(GvtsEngineCtx* ctx) {
    return ctx && ctx->win && ctx->win->isKeyDown(VK_LBUTTON);
}

static void uiSyncMouse(GvtsEngineCtx* ctx) {
    g_uiCursorX = 0;
    g_uiCursorY = 0;
    if (ctx && ctx->win) ctx->win->Gvtcier_Window_CursorPos(g_uiCursorX, g_uiCursorY);
}

static bool uiHit(float x, float y, float w, float h) {
    return (float)g_uiCursorX >= x && (float)g_uiCursorX <= x + w &&
           (float)g_uiCursorY >= y && (float)g_uiCursorY <= y + h;
}

static UiWidgetState& uiState(const std::string& key) {
    return g_uiStates[key];
}

static void uiBeginNewFrame(GvtsEngineCtx* ctx) {
    uiSyncMouse(ctx);
    bool down = uiMouseDownNow(ctx);
    for (auto& kv : g_uiStates) {
        kv.second.hovered = false;
        kv.second.click = false;
        kv.second.released = false;
    }
    g_uiMouseWasDown = down;
}

static void uiEndFrame() {
    for (auto& kv : g_uiStates) g_uiPrevFrame[kv.first] = kv.second.hovered;
}

static void uiDrawPanel(Renderer* r, float x, float y, float w, float h,
                        float cr, float cg, float cb, float ca, float radius) {
    if (radius > 0.0f) r->Gvtcier_Render_DrawGlass(x, y, w, h, radius, ca, 0.0f, gv::kZCanvasImage);
    else r->Gvtcier_Render_DrawRect2D(x, y, w, h, cr, cg, cb, ca);
}

static int g_ui_label(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* text = gvtsG_checkstring(L, 1);
    float x = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float scale = (float)gvtsG_optnumber(L, 4, 1.0);
    float r = (float)gvtsG_optnumber(L, 5, 1.0);
    float g = (float)gvtsG_optnumber(L, 6, 1.0);
    float b = (float)gvtsG_optnumber(L, 7, 1.0);
    float a = (float)gvtsG_optnumber(L, 8, 1.0);
    if (ctx && ctx->renderer && text)
        ctx->renderer->Gvtcier_Render_DrawText((int)x, (int)y, scale, text, gv::kZCanvasImage, r, g, b, a);
    return 0;
}

static int g_ui_centeredLabel(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* text = gvtsG_checkstring(L, 1);
    float cx = (float)gvtsG_checknumber(L, 2);
    float cy = (float)gvtsG_checknumber(L, 3);
    float scale = (float)gvtsG_optnumber(L, 4, 1.0);
    float r = (float)gvtsG_optnumber(L, 5, 1.0);
    float g = (float)gvtsG_optnumber(L, 6, 1.0);
    float b = (float)gvtsG_optnumber(L, 7, 1.0);
    float a = (float)gvtsG_optnumber(L, 8, 1.0);
    if (!ctx || !ctx->renderer || !text) return 0;
    float w = 0.0f, h = 0.0f;
    ctx->renderer->Gvtcier_Render_MeasureText(text, scale, &w, &h);
    ctx->renderer->Gvtcier_Render_DrawText((int)(cx - w * 0.5f), (int)(cy - h * 0.5f),
                                           scale, text, gv::kZCanvasImage, r, g, b, a);
    return 0;
}

static int g_ui_panel(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float w = (float)gvtsG_checknumber(L, 3);
    float h = (float)gvtsG_checknumber(L, 4);
    float r = (float)gvtsG_optnumber(L, 5, 0.06);
    float g = (float)gvtsG_optnumber(L, 6, 0.07);
    float b = (float)gvtsG_optnumber(L, 7, 0.10);
    float a = (float)gvtsG_optnumber(L, 8, 0.86);
    float radius = (float)gvtsG_optnumber(L, 9, 14.0);
    if (!ctx || !ctx->renderer) return 0;
    uiDrawPanel(ctx->renderer, x, y, w, h, r, g, b, a, radius);
    return 0;
}

static int g_ui_button(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* id = gvtsG_checkstring(L, 1);
    const char* text = gvtsG_checkstring(L, 2);
    float x = (float)gvtsG_checknumber(L, 3);
    float y = (float)gvtsG_checknumber(L, 4);
    float w = (float)gvtsG_checknumber(L, 5);
    float h = (float)gvtsG_checknumber(L, 6);
    float scale = (float)gvtsG_optnumber(L, 7, 1.0);
    if (!ctx || !ctx->renderer || !id) { gvts_pushboolean(L, 0); return 1; }

    std::string key(id);
    UiWidgetState& st = uiState(key);
    bool down = uiMouseDownNow(ctx);
    bool hover = uiHit(x, y, w, h);
    st.hovered = hover;

    bool prevDown = g_uiPrevDown.count(key) ? g_uiPrevDown[key] : false;
    bool clicked = hover && down && !prevDown;
    bool released = hover && !down && prevDown;
    g_uiPrevDown[key] = down;
    st.pressed = hover && down;
    st.click = clicked;
    st.released = released;

    float bgR = 0.16f, bgG = 0.18f, bgB = 0.22f;
    float bgA = 0.72f;
    if (hover) { bgR = 0.26f; bgG = 0.30f; bgB = 0.38f; bgA = 0.82f; }
    if (st.pressed) { bgR = 0.42f; bgG = 0.48f; bgB = 0.60f; bgA = 0.90f; }
    ctx->renderer->Gvtcier_Render_DrawRect2D(x, y, w, h, bgR, bgG, bgB, bgA, gv::kZCanvasImage);
    float tw = 0.0f, th = 0.0f;
    ctx->renderer->Gvtcier_Render_MeasureText(text, scale, &tw, &th);
    ctx->renderer->Gvtcier_Render_DrawText((int)(x + (w - tw) * 0.5f), (int)(y + (h - th) * 0.5f),
                                           scale, text ? text : "", gv::kZCanvasImage,
                                           1.0f, 1.0f, 1.0f, hover ? 1.0f : 0.86f);
    gvts_pushboolean(L, clicked ? 1 : 0);
    return 1;
}

static int g_ui_buttonPressed(gvts_State* L) {
    const char* id = gvtsG_checkstring(L, 1);
    auto it = id ? g_uiStates.find(id) : g_uiStates.end();
    gvts_pushboolean(L, (it != g_uiStates.end() && it->second.pressed) ? 1 : 0);
    return 1;
}

static int g_ui_buttonHovered(gvts_State* L) {
    const char* id = gvtsG_checkstring(L, 1);
    auto it = id ? g_uiStates.find(id) : g_uiStates.end();
    gvts_pushboolean(L, (it != g_uiStates.end() && it->second.hovered) ? 1 : 0);
    return 1;
}

static int g_ui_bar(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float w = (float)gvtsG_checknumber(L, 3);
    float h = (float)gvtsG_checknumber(L, 4);
    float v = (float)gvtsG_optnumber(L, 5, 1.0);
    float br = (float)gvtsG_optnumber(L, 6, 0.30);
    float bg = (float)gvtsG_optnumber(L, 7, 0.80);
    float bb = (float)gvtsG_optnumber(L, 8, 0.40);
    if (!ctx || !ctx->renderer) return 0;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    ctx->renderer->Gvtcier_Render_DrawRect2D(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.55f, gv::kZCanvasImage);
    ctx->renderer->Gvtcier_Render_DrawRect2D(x + 1.0f, y + 1.0f, (w - 2.0f) * v, h - 2.0f,
                                             br, bg, bb, 0.95f, gv::kZCanvasImage);
    return 0;
}

static int g_ui_crosshair(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float size = (float)gvtsG_optnumber(L, 1, 8.0);
    float thick = (float)gvtsG_optnumber(L, 2, 2.0);
    float gap = (float)gvtsG_optnumber(L, 3, 3.0);
    if (!ctx || !ctx->renderer || !ctx->win) return 0;
    float cx = ctx->win->width() * 0.5f;
    float cy = ctx->win->height() * 0.5f;
    ctx->renderer->Gvtcier_Render_DrawRect2D(cx - size - gap, cy - thick * 0.5f, size, thick, 1, 1, 1, 0.85f, gv::kZCanvasImage);
    ctx->renderer->Gvtcier_Render_DrawRect2D(cx + gap, cy - thick * 0.5f, size, thick, 1, 1, 1, 0.85f, gv::kZCanvasImage);
    ctx->renderer->Gvtcier_Render_DrawRect2D(cx - thick * 0.5f, cy - size - gap, thick, size, 1, 1, 1, 0.85f, gv::kZCanvasImage);
    ctx->renderer->Gvtcier_Render_DrawRect2D(cx - thick * 0.5f, cy + gap, thick, size, 1, 1, 1, 0.85f, gv::kZCanvasImage);
    return 0;
}

static int g_ui_screenSize(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int w = 0, h = 0;
    if (ctx && ctx->win) { w = ctx->win->width(); h = ctx->win->height(); }
    gvts_pushinteger(L, w);
    gvts_pushinteger(L, h);
    return 2;
}

void Gvtcier_Ui_Tick(GvtsEngineCtx* ctx) {
    uiBeginNewFrame(ctx);
}

void Gvtcier_Ui_End() {
    uiEndFrame();
}

void Gvtcier_Bind_UiControls(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"控件", u8"绘制_标签", u8"绘制文本：文本, x, y, 缩放, 颜色RGBA", g_ui_label);
    regCap(L, ctx, u8"控件", u8"绘制_居中标签", u8"按中心点绘制文本：文本, 中心X, 中心Y, 缩放, 颜色RGBA", g_ui_centeredLabel);
    regCap(L, ctx, u8"控件", u8"绘制_面板", u8"绘制圆角面板：x, y, 宽, 高, 颜色RGB, 透明度, 圆角", g_ui_panel);
    regCap(L, ctx, u8"控件", u8"绘制_按钮", u8"绘制按钮并返回本帧是否被点击：标识, 文本, x, y, 宽, 高, 缩放", g_ui_button);
    regCap(L, ctx, u8"控件", u8"检测_按钮按下", u8"按钮是否处于按下状态：标识", g_ui_buttonPressed);
    regCap(L, ctx, u8"控件", u8"检测_按钮悬停", u8"鼠标是否悬停在按钮上：标识", g_ui_buttonHovered);
    regCap(L, ctx, u8"控件", u8"绘制_进度条", u8"绘制进度条：x, y, 宽, 高, 值0到1, 颜色RGB", g_ui_bar);
    regCap(L, ctx, u8"控件", u8"绘制_准星", u8"绘制屏幕中心准星：臂长, 线宽, 间隙", g_ui_crosshair);
    regCap(L, ctx, u8"控件", u8"获取_屏幕尺寸", u8"返回窗口宽高像素", g_ui_screenSize);
}
