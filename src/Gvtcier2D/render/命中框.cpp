#include "Gvtcier.h"

GvtHitRect Gvtcier_Hit_FromText(Renderer& r, const char* utf8, float scale, float x, float y) {
    GvtHitRect hit;
    if (!utf8 || !utf8[0]) return hit;
    float w = 0.0f, h = 0.0f, top = 0.0f;
    r.Gvtcier_Render_MeasureTextEx(utf8, scale, &w, &h, &top);
    hit.x = x;
    hit.y = y + top;
    hit.w = w;
    hit.h = h;
    return hit;
}

GvtHitRect Gvtcier_Hit_DrawText(Renderer& r, float x, float y, float scale, const char* utf8, int z) {
    GvtHitRect hit = Gvtcier_Hit_FromText(r, utf8, scale, x, y);
    if (!utf8 || !utf8[0]) return hit;
    r.Gvtcier_Render_DrawText((int)x, (int)y, scale, utf8, z);
    return hit;
}

GvtHitRect Gvtcier_Hit_DrawTextCentered(Renderer& r, float centerX, float y, float scale,
                                        const char* utf8, int z) {
    GvtHitRect hit = Gvtcier_Hit_FromText(r, utf8, scale, 0.0f, y);
    if (!utf8 || !utf8[0]) return hit;
    float x = centerX - hit.w * 0.5f;
    hit.x = x;
    r.Gvtcier_Render_DrawText((int)x, (int)y, scale, utf8, z);
    return hit;
}

GvtHitRect Gvtcier_Hit_MeasureText(Renderer& r, const char* utf8, float scale, float x, float y) {
    return Gvtcier_Hit_FromText(r, utf8, scale, x, y);
}

GvtHitRect Gvtcier_Hit_RowControl(float contentX, float contentW, float rowTop, float rowH,
                                  float ctrlH) {
    GvtHitRect hit;
    hit.x = contentX;
    hit.w = contentW;
    hit.y = rowTop + (rowH - ctrlH) * 0.5f;
    hit.h = ctrlH;
    return hit;
}
