#include "Gvtcier.h"
#include <cmath>
#include <cstdlib>

namespace {
// 只有一段 logo：白底黑字 "Made in / Gvtcier"。
// 不做淡入，从第一帧就是满不透明。未买断授权需连续展示满 3 秒再淡出，
// 买断授权免除署名义务，展示 1.5 秒即可。
const float kHoldLicensed = 1.5f;
const float kHoldRequired = 3.0f;
const float kFadeOutDur = 0.4f;

const char* kLineTop = "Made in";
const char* kLineMain = "Gvtcier";

// 两行字的绘制缩放系数，和主菜单标题一个量级
const float kTopScale = 1.55f;
const float kMainScale = 3.9f;
}

void Splash::ensureStars() {}

void Splash::drawStars(Renderer& renderer, int w, int h, float alpha, float gain) {
    (void)renderer;
    (void)w;
    (void)h;
    (void)alpha;
    (void)gain;
}

void Splash::Gvtcier_Splash_Begin(Scene& scene, unsigned int texId) {
    (void)scene;
    (void)texId;
    scene3d_ = nullptr;
    logoObj_ = GvtId<Object>();
    logoTexCount_ = 0;
    texA_ = 0;
    texB_ = 0;
    logoTex_[0] = GvtId<::Texture>();
    logoTex_[1] = GvtId<::Texture>();
    elapsed_ = 0.0f;
    workDone_ = false;
    heldTime_ = 0.0f;
    active_ = true;
}

bool Splash::Gvtcier_Splash_Look(float dx, float dy) {
    (void)dx;
    (void)dy;
    return false;
}

void Splash::Gvtcier_Splash_End() {
    active_ = false;
}

void Splash::updateLogo() {}

void Splash::Gvtcier_Splash_Tick(float dt) {
    if (!active_) return;
    elapsed_ += dt;
    // 后台加载完成之前不计时：先把资源准备好，再从那一刻开始数展示时长。
    if (!workDone_) return;
    heldTime_ += dt;
    float hold = licensed_ ? kHoldLicensed : kHoldRequired;
    if (heldTime_ >= hold + kFadeOutDur) active_ = false;
}

void Splash::Gvtcier_Splash_Draw(Renderer& renderer, int w, int h, int windowMode) {
    (void)windowMode;
    if (!active_) return;
    if (w <= 0 || h <= 0) return;
    float hold = licensed_ ? kHoldLicensed : kHoldRequired;
    if (workDone_ && heldTime_ >= hold + kFadeOutDur) {
        active_ = false;
        return;
    }

    float alpha = 1.0f;
    if (workDone_ && heldTime_ > hold) {
        float t = (heldTime_ - hold) / kFadeOutDur;
        if (t > 1.0f) t = 1.0f;
        alpha = 1.0f - t * t * (3.0f - 2.0f * t);
    }

    renderer.Gvtcier_Render_DrawRect2D(0.0f, 0.0f, (float)w, (float)h, 1.0f, 1.0f, 1.0f, alpha, 0);

    float shortSide = (float)(w < h ? w : h);
    float topScale = kTopScale;
    float mainScale = kMainScale;

    float topW = 0.0f, topH = 0.0f, topTop = 0.0f;
    float mainW = 0.0f, mainH = 0.0f, mainTop = 0.0f;
    renderer.Gvtcier_Render_MeasureTextEx(kLineTop, topScale, &topW, &topH, &topTop);
    renderer.Gvtcier_Render_MeasureTextEx(kLineMain, mainScale, &mainW, &mainH, &mainTop);

    float gap = shortSide * 0.006f;
    float blockH = topH + gap + mainH;
    float mainX = ((float)w - mainW) * 0.5f + mainW * 0.06f;
    float topX = mainX - topW * 0.10f;
    float topY = ((float)h - blockH) * 0.5f;

    renderer.Gvtcier_Render_DrawText((int)topX, (int)(topY - topTop), topScale, kLineTop, 2,
                                     0.0f, 0.0f, 0.0f, alpha);
    renderer.Gvtcier_Render_DrawText((int)mainX, (int)(topY + topH + gap - mainTop), mainScale,
                                     kLineMain, 2, 0.0f, 0.0f, 0.0f, alpha);
}
