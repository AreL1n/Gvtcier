#pragma once

class Renderer;

// 界面可点击区域。界面里凡是「能点的东西」，都先算出一块命中矩形，
// 绘制按它画、命中按它判，两边永远对得上，不会再出现「看到的位置点不到、
// 空白的地方反而能点」。
struct GvtHitRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool valid() const { return w > 0.0f && h > 0.0f; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float midX() const { return x + w * 0.5f; }
    float midY() const { return y + h * 0.5f; }

    // 点是否落在矩形内。上左闭、下右开，相邻两块贴合时不会同时命中。
    bool contains(float px, float py) const {
        if (!valid()) return false;
        return px >= x && px < right() && py >= y && py < bottom();
    }

    // 按四个外边各外扩一圈，用于放大热区（小图标、细箭头之类）。
    GvtHitRect inflated(float padX, float padY) const {
        GvtHitRect r;
        r.x = x - padX;
        r.y = y - padY;
        r.w = w + padX * 2.0f;
        r.h = h + padY * 2.0f;
        return r;
    }

    // 原样返回，方便链式书写。
    GvtHitRect inflated(float pad) const { return inflated(pad, pad); }

    // 把矩形挪到以 (nx, ny) 为左上角。
    GvtHitRect movedTo(float nx, float ny) const {
        GvtHitRect r = *this;
        r.x = nx;
        r.y = ny;
        return r;
    }
};

// 从一段文字的包围盒算出命中矩形。x/y 是 DrawText 传入的左上角坐标，
// 字形本身带有上方的排版留白 outTop，这里把它扣掉，让矩形刚好包住可见字形。
GvtHitRect Gvtcier_Hit_FromText(Renderer& r, const char* utf8, float scale, float x, float y);

// 画一行文字并返回它的命中矩形，绘制与热区一次算完。
GvtHitRect Gvtcier_Hit_DrawText(Renderer& r, float x, float y, float scale, const char* utf8, int z);

// 画一段文字，并返回它在水平方向居中的命中矩形。
GvtHitRect Gvtcier_Hit_DrawTextCentered(Renderer& r, float centerX, float y, float scale,
                                        const char* utf8, int z);

// 只量不画，用于滚动列表这类「先判可见再画」的场景。
GvtHitRect Gvtcier_Hit_MeasureText(Renderer& r, const char* utf8, float scale, float x, float y);

// 一组同类控件里的第 index 个：按行高均分出一条横带，
// 再在横带内竖直居中放一个固定高度的控件框。设置页的每一行都用它，
// 保证绘制、命中、滚动三处算出的位置完全一致。
GvtHitRect Gvtcier_Hit_RowControl(float contentX, float contentW, float rowTop, float rowH,
                                  float ctrlH);
