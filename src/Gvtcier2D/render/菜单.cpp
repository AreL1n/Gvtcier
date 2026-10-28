#include "Gvtcier.h"
#include <algorithm>
#include "Gvtcier2D/render/ui_sound.h"

namespace {

// 暂停菜单的 z 值。80 遮罩最低，81 选中条，82 文字，确认对话框整体再抬高一层（84/85/86），
// 保证对话框盖在菜单项之上。
const int kZMenuDim = 80;
const int kZMenuText = 82;
const int kZMenuSel = 81;
const int kZMenuDialogBg = 84;
const int kZMenuDialogText = 86;
const int kZMenuDialogSel = 85;

// 暂停菜单的四个条目，索引即动作：0 继续、1 设置、2 返回主菜单、3 退出到桌面。
const char* kMainItems[4] = { u8"继续游戏", u8"设置", u8"返回到菜单", u8"退出到桌面" };
const int kMainCount = 4;

// 排版常量。标题 1.9 倍字，条目 1.25 倍，条目间距 52 像素；
// 选中条是左侧一根 5x26 的竖线，离文字左边 18 像素。
const float kTitleScale = 1.9f;
const float kItemScale = 1.25f;
const float kItemStep = 52.0f;
const float kSelBarW = 5.0f;
const float kSelBarH = 26.0f;
const float kSelBarGap = 18.0f;

// 确认对话框的内边距与字号。30 是文案与按钮行之间固定留出的间距。
const float kDialogScale = 1.15f;
const float kDialogPadX = 34.0f;
const float kDialogPadY = 26.0f;

// 确认对话框的布局结果：外框、按钮行顶部、"是/否"两个按钮的 x 与宽度。
struct DialogLayout {
    float boxX = 0.0f, boxY = 0.0f, boxW = 0.0f, boxH = 0.0f;
    float rowTop = 0.0f;
    float btnX[2] = { 0.0f, 0.0f };
    float btnW[2] = { 0.0f, 0.0f };
    float btnH = 0.0f;
};

// 计算"确认退出到桌面？"对话框的布局，居中显示。宽度取文案与按钮行里更宽的那个，
// 两个按钮之间固定留 46 像素。
DialogLayout layoutDialog(Renderer& r, int winW, int winH) {
    DialogLayout L;
    const char* kMsg = u8"确认退出到桌面？";
    const char* kYes = u8"是";
    const char* kNo = u8"否";
    float msgW = 0.0f, msgH = 0.0f, msgTop = 0.0f;
    r.Gvtcier_Render_MeasureTextEx(kMsg, kDialogScale, &msgW, &msgH, &msgTop);
    float yesW = 0.0f, yesH = 0.0f, yesTop = 0.0f;
    r.Gvtcier_Render_MeasureTextEx(kYes, kDialogScale, &yesW, &yesH, &yesTop);
    float noW = 0.0f, noH = 0.0f, noTop = 0.0f;
    r.Gvtcier_Render_MeasureTextEx(kNo, kDialogScale, &noW, &noH, &noTop);
    float btnGap = 46.0f;
    float rowW = yesW + btnGap + noW;
    L.btnH = (std::max)(yesH, noH);
    L.boxW = (std::max)(msgW, rowW) + kDialogPadX * 2.0f;
    L.boxH = kDialogPadY * 2.0f + msgH + 30.0f + L.btnH;
    L.boxX = ((float)winW - L.boxW) * 0.5f;
    L.boxY = ((float)winH - L.boxH) * 0.5f;
    L.rowTop = L.boxY + kDialogPadY + msgH + 30.0f;
    float rowX = L.boxX + (L.boxW - rowW) * 0.5f;
    L.btnX[0] = rowX;
    L.btnW[0] = yesW;
    L.btnX[1] = rowX + yesW + btnGap;
    L.btnW[1] = noW;
    return L;
}

}

// 命中测试确认对话框的两个按钮，返回 0 是"是"、1 是"否"、-1 未命中。
// 按钮矩形由上一次 Draw 写进 btnRect_，所以绘制必须先于命中判断。
int PauseMenu::confirmItemAt(int mx, int my, int winW, int winH) const {
    (void)winW;
    (void)winH;
    for (int i = 0; i < 2; ++i) {
        if ((float)mx >= btnRect_[i].x0 && (float)mx <= btnRect_[i].x1 &&
            (float)my >= btnRect_[i].y0 && (float)my <= btnRect_[i].y1) return i;
    }
    return -1;
}

// 设置左上角显示的标题文字（没有 logo 贴图时才用到）。
void PauseMenu::Gvtcier_Menu_SetTitle(const char* title) {
    title_ = title ? title : "";
}

// 设置左上角 logo。给了宽高后才能等比缩放到占窗口宽度 13% 的大小。
void PauseMenu::Gvtcier_Menu_SetLogo(unsigned int tex, float w, float h) {
    logoTex_ = tex;
    logoW_ = w;
    logoH_ = h;
}

// 打开暂停菜单：回到主页面、选中第一项，并把上一轮的退出/返回标志清掉，
// 否则上次留下的 wantQuit_ 会让刚打开的菜单立刻退出游戏。
void PauseMenu::Gvtcier_Menu_Open() {
    page_ = MENU_PAGE_MAIN;
    mainSel_ = 0;
    mouseNav_ = false;
    wantQuit_ = false;
    wantReturnToMenu_ = false;
}

// 关闭菜单（把页面标记成 NONE），所有 Update/Draw 都会因此空转。
void PauseMenu::Gvtcier_Menu_Close() {
    page_ = MENU_PAGE_NONE;
}

// 菜单打开时要求独占鼠标（松开或重新捕获由上层决定），关闭时把鼠标还给游戏。
bool PauseMenu::Gvtcier_Menu_WantsMouse() const {
    return page_ != MENU_PAGE_NONE;
}

// 命中测试主菜单条目。矩形由 Gvtcier_Hit_DrawText 在绘制时算出并写回，
// 所以可点区域与文字本身的位置、大小完全一致。
int PauseMenu::itemAt(int mx, int my, int winW, int winH) const {
    (void)winW;
    (void)winH;
    for (int i = 0; i < kMainCount; ++i) {
        if (itemHit_[i].contains((float)mx, (float)my)) return i;
    }
    return -1;
}

// 暂停菜单每帧输入处理。按当前页面分三种情况，互相独立：
//   退出确认页：左右切换"是/否"，返回键回上级，确认键或点击按钮执行；
//   设置页：全权交给 SettingsUI，返回键或设置页自己的返回按钮回到主页面；
//   主页面：上下键循环选择，鼠标移动也会抢过选中，确认键执行对应动作。
// settingsImplemented 为 false 时"设置"项点了没反应，用于 2D 游戏还没接设置的情形。
void PauseMenu::Gvtcier_Menu_Update(const MenuInput& in, int winW, int winH, bool settingsImplemented) {
    if (page_ == MENU_PAGE_NONE) return;

    if (page_ == MENU_PAGE_QUIT_CONFIRM) {
        if (in.left || in.right) confirmSel_ = 1 - confirmSel_;
        int hov = confirmItemAt(in.mouseX, in.mouseY, winW, winH);
        confirmHover_ = hov;
        if (hov >= 0) confirmSel_ = hov;
        if (in.back) { page_ = MENU_PAGE_MAIN; confirmHover_ = -1; return; }
        if (in.confirm || (in.mouseClicked && hov >= 0)) {
            if (confirmSel_ == 0) wantQuit_ = true;
            else { page_ = MENU_PAGE_MAIN; confirmHover_ = -1; }
            return;
        }
        return;
    }

    if (page_ == MENU_PAGE_SETTINGS) {
        bool wantsBack = false;
        bool changed = false;
        settings_.Gvtcier_SettingsUI_Update(in, winW, winH, &changed, &wantsBack);
        if (changed) settingsChanged_ = true;
        if (in.back || wantsBack) {
            settings_.Gvtcier_SettingsUI_ResetFocus();
            page_ = MENU_PAGE_MAIN;
        }
        return;
    }

    if (in.up || in.down) mouseNav_ = false;
    int selBefore = mainSel_;
    if (in.up) mainSel_ = (mainSel_ + kMainCount - 1) % kMainCount;
    if (in.down) mainSel_ = (mainSel_ + 1) % kMainCount;
    if (in.back) { page_ = MENU_PAGE_NONE; return; }

    int hover = itemAt(in.mouseX, in.mouseY, winW, winH);
    if (in.mouseMoved) mouseNav_ = true;
    if (hover >= 0) {
        mainSel_ = hover;
        mouseNav_ = true;
    } else if (mouseNav_) {
        mainSel_ = -1;
    }
    if (mainSel_ != selBefore && mainSel_ >= 0) {
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiHover);
    }
    if (in.confirm || (in.mouseClicked && hover >= 0)) {
        if (mainSel_ < 0 || mainSel_ >= kMainCount) return;
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);
        if (mainSel_ == 0) {
            page_ = MENU_PAGE_NONE;
        } else if (mainSel_ == 1) {
            if (settingsImplemented) {
                settings_.Gvtcier_SettingsUI_Build(settings_.Gvtcier_SettingsUI_Tab());
                page_ = MENU_PAGE_SETTINGS;
            }
        } else if (mainSel_ == 2) {
            wantReturnToMenu_ = true;
            page_ = MENU_PAGE_NONE;
        } else {
            page_ = MENU_PAGE_QUIT_CONFIRM;
            confirmSel_ = 0;
        }
        return;
    }
}

// 绘制暂停菜单。设置页直接把整个屏幕交给 SettingsUI，主页面则是
// 全屏 22% 黑遮罩 + 左上角 logo/标题 + 底部菜单项，选中项左侧画一根蓝色竖条。
// 退出确认对话框在菜单项之后画，保证压在最上面。
// 注意 btnRect_ 在绘制时顺手写回，供下一帧的 confirmItemAt 命中判断使用。
void PauseMenu::Gvtcier_Menu_Draw(Renderer& r, int winW, int winH) {
    if (page_ == MENU_PAGE_NONE) return;

    if (page_ == MENU_PAGE_SETTINGS) {
        settings_.Gvtcier_SettingsUI_Draw(r, winW, winH);
        return;
    }

    r.Gvtcier_Render_DrawRect2D(0.0f, 0.0f, (float)winW, (float)winH, 0.0f, 0.0f, 0.0f, 0.22f, kZMenuDim);

    if (logoTex_ && logoW_ > 0.0f && logoH_ > 0.0f) {
        float maxW = (float)winW * 0.13f;
        float scale = maxW / logoW_;
        r.Gvtcier_Render_DrawImage(logoTex_, 40.0f, 40.0f, logoW_ * scale, logoH_ * scale, 1.0f, kZMenuText);
    } else if (!title_.empty()) {
        r.Gvtcier_Render_DrawText(40, 40, kTitleScale, title_.c_str(), kZMenuText);
    }

    float baseY = (float)winH - 40.0f - kItemStep * (float)kMainCount;
    for (int i = 0; i < kMainCount; ++i) {
        float iy = baseY + kItemStep * (float)i;
        itemHit_[i] = Gvtcier_Hit_DrawText(r, 40.0f, iy, kItemScale, kMainItems[i], kZMenuText);
        if (i == mainSel_) {
            float barY = itemHit_[i].midY() - kSelBarH * 0.5f;
            r.Gvtcier_Render_DrawRect2D(40.0f - kSelBarGap, barY, kSelBarW, kSelBarH,
                                        0.35f, 0.75f, 1.0f, 0.95f, kZMenuSel);
        }
    }

    if (page_ != MENU_PAGE_QUIT_CONFIRM) return;

    const char* kMsg = u8"确认退出到桌面？";
    const char* kYes = u8"是";
    const char* kNo = u8"否";

    DialogLayout L = layoutDialog(r, winW, winH);
    float msgW = 0.0f, msgH = 0.0f, msgTop = 0.0f;
    r.Gvtcier_Render_MeasureTextEx(kMsg, kDialogScale, &msgW, &msgH, &msgTop);

    r.Gvtcier_Render_DrawRect2D(L.boxX, L.boxY, L.boxW, L.boxH, 0.07f, 0.08f, 0.10f, 0.96f, kZMenuDialogBg);
    r.Gvtcier_Render_DrawRect2D(L.boxX, L.boxY, L.boxW, 2.0f, 0.30f, 0.33f, 0.38f, 0.9f, kZMenuDialogBg);
    float msgY = L.boxY + kDialogPadY - msgTop;
    r.Gvtcier_Render_DrawText((int)(L.boxX + (L.boxW - msgW) * 0.5f), (int)msgY,
                              kDialogScale, kMsg, kZMenuDialogText);

    const char* btns[2] = { kYes, kNo };
    for (int i = 0; i < 2; ++i) {
        float bw = 0.0f, bh = 0.0f, bt = 0.0f;
        r.Gvtcier_Render_MeasureTextEx(btns[i], kDialogScale, &bw, &bh, &bt);
        float by = L.rowTop - bt;
        btnRect_[i].x0 = L.btnX[i] - 8.0f;
        btnRect_[i].y0 = L.rowTop - 6.0f;
        btnRect_[i].x1 = L.btnX[i] + L.btnW[i] + 8.0f;
        btnRect_[i].y1 = L.rowTop + L.btnH + 8.0f;
        if (i == confirmSel_)
            r.Gvtcier_Render_DrawRect2D(btnRect_[i].x0, btnRect_[i].y0,
                                        btnRect_[i].x1 - btnRect_[i].x0,
                                        btnRect_[i].y1 - btnRect_[i].y0,
                                        0.20f, 0.42f, 0.62f, 0.85f, kZMenuDialogSel);
        r.Gvtcier_Render_DrawText((int)L.btnX[i], (int)by, kDialogScale, btns[i], kZMenuDialogText);
    }
}
