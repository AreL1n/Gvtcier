#include "Gvtcier.h"

// 脚本绑定：UI 界面。控件容器与整体界面的显隐、层级管理。
// 控件本身的创建与交互见 控件.cpp。

#ifndef VK_LBUTTON
#define VK_LBUTTON 0x01
#endif
#ifndef VK_RBUTTON
#define VK_RBUTTON 0x02
#endif
#ifndef VK_MBUTTON
#define VK_MBUTTON 0x04
#endif

static int g_cr_drawText(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int x = (int)gvtsG_checknumber(L, 1);
    int y = (int)gvtsG_checknumber(L, 2);
    double scale = gvtsG_checknumber(L, 3);
    const char* utf8 = gvtsG_checkstring(L, 4);
    if (!ctx->renderer || !utf8) return 0;
    ctx->renderer->Gvtcier_Render_DrawText(x, y, (float)scale, utf8);
    return 0;
}

static int g_cr_drawRect(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float w = (float)gvtsG_checknumber(L, 3);
    float h = (float)gvtsG_checknumber(L, 4);
    float r = (float)gvtsG_optnumber(L, 5, 1.0);
    float g = (float)gvtsG_optnumber(L, 6, 1.0);
    float bl = (float)gvtsG_optnumber(L, 7, 1.0);
    float a = (float)gvtsG_optnumber(L, 8, 1.0);
    if (!ctx->renderer) return 0;
    ctx->renderer->Gvtcier_Render_DrawRect2D(x, y, w, h, r, g, bl, a);
    return 0;
}

static int g_cr_drawImage(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* name = gvtsG_checkstring(L, 1);
    float x = (float)gvtsG_checknumber(L, 2);
    float y = (float)gvtsG_checknumber(L, 3);
    float w = (float)gvtsG_optnumber(L, 4, 0.0);
    float h = (float)gvtsG_optnumber(L, 5, 0.0);
    float a = (float)gvtsG_optnumber(L, 6, 1.0);
    if (!ctx->renderer || !ctx->scene || !name) return 0;
    Texture* t = ctx->scene->Gvtcier_Scene_GetTex(ctx->scene->Gvtcier_Scene_LoadPngTexture(name));
    if (!t || t->id() == 0) return 0;
    if (w <= 0.0f) w = (float)t->width();
    if (h <= 0.0f) h = (float)t->height();
    ctx->renderer->Gvtcier_Render_DrawImage(t->id(), x, y, w, h, a, gv::kZCanvasImage);
    return 0;
}

static int g_ui_mousePos(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int x = 0, y = 0;
    if (ctx->win) ctx->win->Gvtcier_Window_CursorPos(x, y);
    gvts_pushinteger(L, x);
    gvts_pushinteger(L, y);
    return 2;
}

static int g_ui_mouseDown(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int btn = (int)gvtsG_optinteger(L, 1, 0);
    int vk = VK_LBUTTON;
    if (btn == 1) vk = VK_RBUTTON;
    else if (btn == 2) vk = VK_MBUTTON;
    gvts_pushboolean(L, ctx->win && ctx->win->isKeyDown(vk));
    return 1;
}

static int g_ui_hover(gvts_State* L) {
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float w = (float)gvtsG_checknumber(L, 3);
    float h = (float)gvtsG_checknumber(L, 4);
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    int mx = 0, my = 0;
    if (ctx->win) ctx->win->Gvtcier_Window_CursorPos(mx, my);
    gvts_pushboolean(L, (float)mx >= x && (float)mx <= x + w &&
                       (float)my >= y && (float)my <= y + h);
    return 1;
}

static int g_ui_mouseVisible(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->win) ctx->win->Gvtcier_Window_GrabMouse(false);
    return 0;
}

static int g_ui_mouseCapture(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->win) ctx->win->Gvtcier_Window_GrabMouse(true);
    return 0;
}

static int g_ui_textWidth(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    const char* utf8 = gvtsG_checkstring(L, 1);
    float scale = (float)gvtsG_optnumber(L, 2, 1.0);
    float w = 0.0f, h = 0.0f;
    if (ctx->renderer && utf8) ctx->renderer->Gvtcier_Render_MeasureText(utf8, scale, &w, &h);
    gvts_pushinteger(L, (gvts_Integer)(w + 0.5f));
    gvts_pushinteger(L, (gvts_Integer)(h + 0.5f));
    return 2;
}

void Gvtcier_Bind_Ui(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"绘制", u8"绘制_文本", u8"屏幕坐标绘制文本", g_cr_drawText);
    regCap(L, ctx, u8"绘制", u8"绘制_矩形", u8"屏幕坐标绘制矩形", g_cr_drawRect);
    regCap(L, ctx, u8"绘制", u8"绘制_图片", u8"绘制 png 图片，宽高省略时用原图尺寸", g_cr_drawImage);
    regCap(L, ctx, u8"界面", u8"获取_鼠标位置", u8"返回鼠标在窗口内的像素坐标 x, y，交互界面用", g_ui_mousePos);
    regCap(L, ctx, u8"界面", u8"检测_鼠标按下", u8"查询鼠标键是否按下，0 左键 1 右键 2 中键", g_ui_mouseDown);
    regCap(L, ctx, u8"界面", u8"检测_鼠标悬停", u8"查询鼠标是否落在给定矩形内，做按钮热区用", g_ui_hover);
    regCap(L, ctx, u8"界面", u8"设置_显示鼠标", u8"松开鼠标捕获，让光标可见可点界面", g_ui_mouseVisible);
    regCap(L, ctx, u8"界面", u8"设置_捕获鼠标", u8"重新捕获鼠标用于 3D 视角控制", g_ui_mouseCapture);
    regCap(L, ctx, u8"界面", u8"获取_测量文本", u8"返回文本渲染后的像素宽高，用于排版与热区", g_ui_textWidth);
}
