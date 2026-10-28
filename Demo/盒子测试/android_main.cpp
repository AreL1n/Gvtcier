#include "Gvtcier3D/render/renderer.h"
#include "bicu/platform/gvw.h"

namespace {

const int kPadMargin = 24;

struct PadRect {
    int x;
    int y;
    int w;
    int h;
};

int padSize(const GvWWindow* w) {
    int s = w ? w->height / 5 : 120;
    if (s < 90) s = 90;
    if (s > 240) s = 240;
    return s;
}

PadRect wasdRect(const GvWWindow* w, int col, int row) {
    int s = padSize(w);
    int gap = s / 6;
    PadRect r;
    r.w = s;
    r.h = s;
    r.x = kPadMargin + col * (s + gap);
    r.y = w->height - kPadMargin - s - (1 - row) * (s + gap);
    return r;
}

PadRect jumpRect(const GvWWindow* w) {
    int s = padSize(w) * 3 / 2;
    PadRect r;
    r.w = s;
    r.h = s;
    r.x = w->width - kPadMargin - s;
    r.y = w->height - kPadMargin - s;
    return r;
}

void drawKey(Renderer& r, const PadRect& rc, const char* label, bool down) {
    float x = (float)rc.x;
    float y = (float)rc.y;
    float w = (float)rc.w;
    float h = (float)rc.h;
    float pad = (float)rc.w * 0.08f;
    if (down) {
        r.Gvtcier_Render_DrawRect2D(x, y, w, h, 0.95f, 0.85f, 0.35f, 0.95f, 900);
        r.Gvtcier_Render_DrawRect2D(x + pad, y + pad, w - pad * 2.0f, h - pad * 2.0f, 0.85f, 0.72f, 0.25f, 1.0f, 901);
    } else {
        r.Gvtcier_Render_DrawRect2D(x, y, w, h, 0.10f, 0.12f, 0.16f, 0.55f, 900);
        r.Gvtcier_Render_DrawRect2D(x + pad, y + pad, w - pad * 2.0f, h - pad * 2.0f, 0.22f, 0.25f, 0.32f, 0.75f, 901);
    }
    float tw = 0.0f;
    float th = 0.0f;
    float scale = (float)rc.h * 0.012f;
    r.Gvtcier_Render_MeasureText(label, scale, &tw, &th);
    int tx = rc.x + (int)((w - tw) * 0.5f);
    int ty = rc.y + (int)((h - th) * 0.5f);
    if (down) r.Gvtcier_Render_DrawText(tx, ty, scale, label, 902, 0.15f, 0.12f, 0.05f, 1.0f);
    else r.Gvtcier_Render_DrawText(tx, ty, scale, label, 902, 0.92f, 0.93f, 0.96f, 1.0f);
}

}

extern "C" __attribute__((visibility("default"))) void Gvtcier_Android_DrawOverlay(Renderer& r, const GvWWindow* w) {
    if (!w) return;
    drawKey(r, wasdRect(w, 1, 0), "W", GvW_IsKeyDown(w, 'W'));
    drawKey(r, wasdRect(w, 0, 1), "A", GvW_IsKeyDown(w, 'A'));
    drawKey(r, wasdRect(w, 1, 1), "S", GvW_IsKeyDown(w, 'S'));
    drawKey(r, wasdRect(w, 2, 1), "D", GvW_IsKeyDown(w, 'D'));
    drawKey(r, jumpRect(w), "跳", GvW_IsKeyDown(w, ' '));
}
