#include "Gvtcier.h"

// 脚本绑定：二维画布。脚本直接绘制矩形、文字、图片到屏幕。
// 与 3D 的渲染管线无关，全部走 Canvas2D 的立即模式队列，在帧末统一提交。

// 画矩形：参数 x, y, 宽, 高, [颜色]。
static int g_cr_c2Rect(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->canvas2d) return 0;
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float w = (float)gvtsG_checknumber(L, 3);
    float h = (float)gvtsG_checknumber(L, 4);
    float rad = (float)gvtsG_optnumber(L, 5, 0.0);
    float r = (float)gvtsG_optnumber(L, 6, 1.0);
    float g = (float)gvtsG_optnumber(L, 7, 1.0);
    float bl = (float)gvtsG_optnumber(L, 8, 1.0);
    float a = (float)gvtsG_optnumber(L, 9, 1.0);
    C2Color c = { r, g, bl, a };
    ctx->canvas2d->Gvtcier_Canvas2D_RoundRect(x, y, w, h, rad, 0.0f, c, 0, 0.0f);
    return 0;
}

static int g_cr_c2Circle(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->canvas2d) return 0;
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float rad = (float)gvtsG_checknumber(L, 3);
    float r = (float)gvtsG_optnumber(L, 4, 1.0);
    float g = (float)gvtsG_optnumber(L, 5, 1.0);
    float bl = (float)gvtsG_optnumber(L, 6, 1.0);
    float a = (float)gvtsG_optnumber(L, 7, 1.0);
    C2Color c = { r, g, bl, a };
    ctx->canvas2d->Gvtcier_Canvas2D_Circle(x, y, rad, 0.0f, c, 0, 0.0f);
    return 0;
}

static int g_cr_c2Line(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->canvas2d) return 0;
    float x0 = (float)gvtsG_checknumber(L, 1);
    float y0 = (float)gvtsG_checknumber(L, 2);
    float x1 = (float)gvtsG_checknumber(L, 3);
    float y1 = (float)gvtsG_checknumber(L, 4);
    float th = (float)gvtsG_optnumber(L, 5, 2.0);
    float r = (float)gvtsG_optnumber(L, 6, 1.0);
    float g = (float)gvtsG_optnumber(L, 7, 1.0);
    float bl = (float)gvtsG_optnumber(L, 8, 1.0);
    float a = (float)gvtsG_optnumber(L, 9, 1.0);
    C2Color c = { r, g, bl, a };
    ctx->canvas2d->Gvtcier_Canvas2D_Line(x0, y0, x1, y1, th, c, 0, 0.0f, 0.0f);
    return 0;
}

static int g_cr_c2Arc(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (!ctx->canvas2d) return 0;
    float x = (float)gvtsG_checknumber(L, 1);
    float y = (float)gvtsG_checknumber(L, 2);
    float rad = (float)gvtsG_checknumber(L, 3);
    float th = (float)gvtsG_checknumber(L, 4);
    float a0 = (float)gvtsG_checknumber(L, 5);
    float a1 = (float)gvtsG_checknumber(L, 6);
    float r = (float)gvtsG_optnumber(L, 7, 1.0);
    float g = (float)gvtsG_optnumber(L, 8, 1.0);
    float bl = (float)gvtsG_optnumber(L, 9, 1.0);
    float a = (float)gvtsG_optnumber(L, 10, 1.0);
    C2Color c = { r, g, bl, a };
    ctx->canvas2d->Gvtcier_Canvas2D_Arc(x, y, rad, th, a0, a1, c, 0, 0.0f);
    return 0;
}

void Gvtcier_Bind_Canvas2d(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"绘制", u8"绘制_圆角矩形", u8"2D 画布绘制圆角矩形", g_cr_c2Rect);
    regCap(L, ctx, u8"绘制", u8"绘制_圆", u8"2D 画布绘制圆", g_cr_c2Circle);
    regCap(L, ctx, u8"绘制", u8"绘制_线", u8"2D 画布绘制线段", g_cr_c2Line);
    regCap(L, ctx, u8"绘制", u8"绘制_弧", u8"2D 画布绘制圆弧", g_cr_c2Arc);
}
