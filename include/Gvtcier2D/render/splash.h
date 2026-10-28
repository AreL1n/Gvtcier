#pragma once
#include "bicu/core/export.h"
#include "bicu/core/math.h"
#include "bicu/core/identity.h"

class Renderer;
class Scene;
class Texture;
struct Object;

// 启动画面：白底黑字的「Made in / Gvtcier」，直接显示后淡出。
// 未买断授权下须连续展示不少于 3 秒，买断后缩短到 1.5 秒。
class GVTCIER_API Splash {
public:
    void Gvtcier_Splash_Begin(Scene& scene, unsigned int texId);
    void Gvtcier_Splash_SetDev(bool dev) { dev_ = dev; }
    // 买断授权直接免除署名义务，可把展示时长压到 1.5 秒
    void Gvtcier_Splash_SetLicensed(bool licensed) { licensed_ = licensed; }
    bool Gvtcier_Splash_Active() const { return active_; }
    // 通知启动画面：后台加载已完成。展示时长从这一刻起算。
    void Gvtcier_Splash_SetWorkDone() { workDone_ = true; }
    bool Gvtcier_Splash_WorkDone() const { return workDone_; }
    void Gvtcier_Splash_End();
    void Gvtcier_Splash_Tick(float dt);
    void Gvtcier_Splash_Draw(Renderer& renderer, int w, int h, int windowMode = 3);
    float Gvtcier_Splash_Elapsed() const { return elapsed_; }
    bool Gvtcier_Splash_Look(float dx, float dy);

private:
    void ensureStars();
    void drawStars(Renderer& renderer, int w, int h, float alpha, float gain);
    void updateLogo();

    bool active_ = false;
    bool dev_ = false;
    bool licensed_ = false;
    float elapsed_ = 0.0f;
    // 后台加载还没完成时不计入展示时长：先把资源准备好，再从「加载完成」那一刻开始计时。
    bool workDone_ = true;
    float heldTime_ = 0.0f;
    unsigned int texA_ = 0;
    unsigned int texB_ = 0;
    float aw_ = 0.0f, ah_ = 0.0f;
    float bw_ = 0.0f, bh_ = 0.0f;
    Scene* scene3d_ = nullptr;
    GvtId<Object> logoObj_;
    GvtId<::Texture> logoTex_[2];
    int logoTexCount_ = 0;
    float starDrift_ = 0.0f;
};
