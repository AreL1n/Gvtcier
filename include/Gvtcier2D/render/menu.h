#pragma once
#include <string>
#include "bicu/core/export.h"
#include "Gvtcier2D/render/hit_rect.h"
#include "Gvtcier2D/render/settings_ui.h"

class Renderer;

// 暂停菜单当前显示的页面。
enum MenuPage {
    MENU_PAGE_NONE = 0,     // 菜单关闭
    MENU_PAGE_MAIN,         // 主条目列表
    MENU_PAGE_SETTINGS,     // 设置页
    MENU_PAGE_QUIT_CONFIRM  // 退出确认对话框
};

// 每帧喂给菜单的输入快照。菜单自己不做输入查询，全部由上层填好，
// 这样键盘鼠标手柄可以统一成同一套语义。
struct MenuInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false;     // 确认/回车/点击
    bool back = false;        // 返回/Esc
    int mouseX = 0;
    int mouseY = 0;
    bool mouseDown = false;   // 左键当前是否按住
    bool mouseClicked = false;// 本帧发生了一次点击
    bool mouseMoved = false;
    bool keyNav = false;      // 本帧有方向键输入，用于在鼠标选中与键盘选中之间切换
    int capturedKey = 0;      // 本帧捕获到的虚拟键码，设置页绑定按键时用
};

// 暂停菜单。既负责主条目列表，也内嵌一个 SettingsUI 与退出确认对话框。
class GVTCIER_API PauseMenu {
public:
    void Gvtcier_Menu_SetTitle(const char* title);
    void Gvtcier_Menu_SetLogo(unsigned int tex, float w, float h);

    bool Gvtcier_Menu_IsOpen() const { return page_ != MENU_PAGE_NONE; }
    // 玩家选择了"退出到桌面"。这是标志位，不会自动清，由上层读取后自己处理。
    bool Gvtcier_Menu_WantsQuit() const { return wantQuit_; }
    // 取走"返回主菜单"请求并清零，一次性语义，避免重复触发。
    bool Gvtcier_Menu_ConsumeReturnToMenu() {
        bool r = wantReturnToMenu_;
        wantReturnToMenu_ = false;
        return r;
    }
    void Gvtcier_Menu_Open();
    void Gvtcier_Menu_Close();
    bool Gvtcier_Menu_WantsMouse() const;

    void Gvtcier_Menu_Update(const MenuInput& in, int winW, int winH, bool settingsImplemented);
    void Gvtcier_Menu_Draw(Renderer& r, int winW, int winH);
    SettingsUI& Gvtcier_Menu_Settings() { return settings_; }
    // 取走"设置被改动过"标志并清零，用于触发配置保存。
    bool Gvtcier_Menu_ConsumeSettingsChanged() {
        bool c = settingsChanged_;
        settingsChanged_ = false;
        return c;
    }

private:
    int itemAt(int mx, int my, int winW, int winH) const;
    int confirmItemAt(int mx, int my, int winW, int winH) const;

    int page_ = MENU_PAGE_NONE;   // 当前页面
    // 条目的可点击矩形，绘制时按文字包围盒算出写回，命中判断直接读它。
    GvtHitRect itemHit_[4];
    int mainSel_ = 0;             // 主条目选中下标，-1 表示鼠标不在任何项上
    bool mouseNav_ = false;       // 当前选中是否来自鼠标
    int confirmSel_ = 0;          // 确认对话框选中项，0 是 1 否
    int confirmHover_ = -1;       // 确认对话框鼠标悬停的按钮
    // 确认对话框按钮的命中矩形，绘制时填、下一帧命中时读。
    struct BtnHit { float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
    BtnHit btnRect_[2];
    int settingsTab_ = 0;         // 预留的设置页记录，当前实际标签页由 SettingsUI 自己管
    bool wantQuit_ = false;       // 请求退出到桌面
    bool wantReturnToMenu_ = false; // 请求返回主菜单
    std::string title_;
    unsigned int logoTex_ = 0;
    float logoW_ = 0.0f;
    float logoH_ = 0.0f;
    SettingsUI settings_;
    bool settingsChanged_ = false;
};
