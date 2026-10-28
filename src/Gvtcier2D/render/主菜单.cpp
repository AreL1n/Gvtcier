#include "Gvtcier.h"
#include <algorithm>
#include "Gvtcier2D/render/ui_sound.h"

namespace {

// 主菜单 z 值：86 背景图最低，88 黑色蒙版，91 选中条，92 文字 / 90 标题。
// 注意标题 z 反而比条目低，靠绘制顺序保证标题先画，实际不会互相遮挡。
const int kZTitle = 90;
const int kZItem = 92;
const int kZSelBar = 91;
const int kZOverlay = 88;
const int kZBg = 86;

// 三个条目：0 开始游戏、1 设置、2 退出。
const char* kItems[3] = { u8"开始游戏", u8"设置", u8"退出" };
const int kItemCount = 3;

// 排版常量。标题 2.4 倍字放左上，条目 1.35 倍、间距 56 像素，从底部往上排，
// 左右边距 48，底部留白 56。
const float kTitleScale = 2.4f;
const float kItemScale = 1.35f;
const float kItemStep = 56.0f;
const float kSelBarW = 5.0f;
const float kSelBarH = 28.0f;
const float kSelBarGap = 18.0f;
const float kMarginX = 48.0f;
const float kMarginBottom = 56.0f;

// 第 index 个条目的顶部 y。绘制与命中都用它，保证两边算出的位置一致。
float itemYAt(int index, int winH) {
    float baseY = (float)winH - kMarginBottom - kItemStep * (float)kItemCount;
    return baseY + kItemStep * (float)index;
}

}

// 设置标题文字。传空指针按空串处理，绘制时会自动跳过空标题。
void MainMenu::Gvtcier_MainMenu_SetTitle(const char* title) {
    title_ = title ? title : "";
}

// 设置背景模式，只接受 2D/3D 两个值，其他非法值一律回落成 2D。
void MainMenu::Gvtcier_MainMenu_SetBgMode(int mode) {
    bgMode_ = (mode == MAINMENU_BG_3D) ? MAINMENU_BG_3D : MAINMENU_BG_2D;
}

// 只给纹理不给尺寸，绘制时直接铺满窗口（会拉伸）。
void MainMenu::Gvtcier_MainMenu_SetBgImage(unsigned int tex) {
    bgTex_ = tex;
}

// 给纹理和原始尺寸，绘制时会按"铺满窗口同时保持比例"的方式裁切缩放。
void MainMenu::Gvtcier_MainMenu_SetBgImage(unsigned int tex, int imgW, int imgH) {
    bgTex_ = tex;
    bgTexW_ = imgW;
    bgTexH_ = imgH;
}

// 设置 3D 背景要加载的 gmap 路径。这个值只是记下来，实际加载由上层读 bgScenePath_ 完成，
// 因为本模块不持有 Scene 引用。
void MainMenu::Gvtcier_MainMenu_SetBgScene(const char* gmapPath) {
    bgScenePath_ = gmapPath ? gmapPath : "";
}

// 打开主菜单：进入条目页、选中第一项，清掉"刚打开"与点击冷却状态。
// justOpened_ 会在第一次 Update 时把点击屏蔽掉，防止关掉暂停菜单的那一下点击顺带触发开始游戏。
void MainMenu::Gvtcier_MainMenu_Open() {
    open_ = true;
    justOpened_ = true;
    page_ = 0;
    sel_ = 0;
    mouseNav_ = false;
    wantQuit_ = false;
    wantStart_ = false;
}

// 关闭主菜单，Draw/Update 都会直接返回。
void MainMenu::Gvtcier_MainMenu_Close() {
    open_ = false;
}

// 累计运行时间，用于点击冷却计时（只用推进，不做别的）。
void MainMenu::Gvtcier_MainMenu_Tick(float dt) {
    time_ += dt;
}

// 取走"开始游戏"请求并清零，一次性语义。
bool MainMenu::Gvtcier_MainMenu_ConsumeStart() {
    bool s = wantStart_;
    wantStart_ = false;
    return s;
}

// 命中测试主菜单条目。矩形由 Gvtcier_Hit_DrawText 在绘制时算出并写回，
// 所以可点区域与文字本身的位置、大小完全一致。
int MainMenu::itemAt(int mx, int my, int winW, int winH) const {
    (void)winW;
    (void)winH;
    for (int i = 0; i < kItemCount; ++i) {
        if (itemHit_[i].contains((float)mx, (float)my)) return i;
    }
    return -1;
}

// 主菜单每帧输入处理。
// 点击屏蔽逻辑：刚打开的第一帧、以及距离上一次点击 0.1 秒内的点击都算误触，
// 这是为了防止"关闭上一界面"的那一下点击穿透到主菜单。
// 设置页时把点击/确认过滤后转交给 SettingsUI，自己只关心返回。
// 条目页支持上下键循环选择与鼠标悬停选中；选中后按情况置开始/退出标志或进入设置页。
void MainMenu::Gvtcier_MainMenu_Update(const MenuInput& in, int winW, int winH) {
    if (!open_) return;

    bool clickBlocked = justOpened_ ||
                        (lastClickTime_ >= 0.0f && time_ - lastClickTime_ < 0.1f);
    justOpened_ = false;
    bool clickEdge = in.mouseClicked && !clickBlocked;
    if (clickEdge) lastClickTime_ = time_;

    if (page_ == 1) {
        bool back = false;
        MenuInput sIn = in;
        sIn.mouseClicked = clickEdge;
        sIn.confirm = in.confirm && !clickBlocked;
        settings_.Gvtcier_SettingsUI_Update(sIn, winW, winH, &settingsChanged_, &back);
        if (back) page_ = 0;
        return;
    }

    if (in.up || in.down) {
        mouseNav_ = false;
        if (sel_ < 0) sel_ = 0;
    }
    int selBefore = sel_;
    if (in.up) sel_ = (sel_ + kItemCount - 1) % kItemCount;
    if (in.down) sel_ = (sel_ + 1) % kItemCount;

    if (in.mouseMoved) {
        mouseNav_ = true;
        int h = itemAt(in.mouseX, in.mouseY, winW, winH);
        sel_ = h;
    }

    if (sel_ != selBefore && sel_ >= 0) {
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiHover);
    }

    bool activate = in.confirm && !clickBlocked;
    if (clickEdge) {
        int h = itemAt(in.mouseX, in.mouseY, winW, winH);
        if (h >= 0) {
            sel_ = h;
            activate = true;
        }
    }
    if (!activate) return;
    if (sel_ < 0 || sel_ >= kItemCount) return;

    GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);

    switch (sel_) {
        case 0:
            wantStart_ = true;
            open_ = false;
            break;
        case 1:
            settings_.Gvtcier_SettingsUI_Build(settings_.Gvtcier_SettingsUI_Tab());
            page_ = 1;
            lastClickTime_ = time_;
            settings_.Gvtcier_SettingsUI_ResetFocus();
            break;
        case 2:
            wantQuit_ = true;
            break;
        default:
            break;
    }
}

// 绘制主菜单。设置页时整屏交给 SettingsUI 并直接返回。
// 否则先画 2D 背景图（有原始尺寸时按 cover 方式等比裁切铺满，没有就硬拉伸），
// 再盖一层 28% 黑蒙版压暗背景以保证文字可读，最后画标题与条目。
void MainMenu::Gvtcier_MainMenu_Draw(Renderer& r, int winW, int winH) {
    if (!open_) return;

    if (page_ == 1) {
        settings_.Gvtcier_SettingsUI_Draw(r, winW, winH);
        return;
    }

    if (bgMode_ == MAINMENU_BG_2D && bgTex_) {
        if (bgTexW_ > 0 && bgTexH_ > 0) {
            float sw = (float)winW, sh = (float)winH;
            float srcAspect = (float)bgTexW_ / (float)bgTexH_;
            float dstAspect = sw / sh;
            float dw = sw, dh = sh, dx = 0.0f, dy = 0.0f;
            if (srcAspect > dstAspect) {
                dh = sh;
                dw = sh * srcAspect;
                dx = (sw - dw) * 0.5f;
            } else {
                dw = sw;
                dh = sw / srcAspect;
                dy = (sh - dh) * 0.5f;
            }
            r.Gvtcier_Render_DrawImage(bgTex_, dx, dy, dw, dh, 1.0f, kZBg);
        } else {
            r.Gvtcier_Render_DrawImage(bgTex_, 0.0f, 0.0f, (float)winW, (float)winH, 1.0f, kZBg);
        }
    }

    r.Gvtcier_Render_DrawRect2D(0.0f, 0.0f, (float)winW, (float)winH,
                                0.0f, 0.0f, 0.0f, 0.28f, kZOverlay);

    if (!title_.empty()) {
        r.Gvtcier_Render_DrawText((int)kMarginX, 56, kTitleScale, title_.c_str(), kZTitle);
    }

    for (int i = 0; i < kItemCount; ++i) {
        float iy = itemYAt(i, winH);
        itemHit_[i] = Gvtcier_Hit_DrawText(r, kMarginX, iy, kItemScale, kItems[i], kZItem);
        if (i == sel_) {
            float barY = itemHit_[i].midY() - kSelBarH * 0.5f;
            r.Gvtcier_Render_DrawRect2D(kMarginX - kSelBarGap, barY, kSelBarW, kSelBarH,
                                        0.35f, 0.75f, 1.0f, 0.95f, kZSelBar);
        }
    }
}
