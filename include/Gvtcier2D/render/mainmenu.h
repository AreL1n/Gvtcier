#pragma once
#include <string>
#include "bicu/core/export.h"
#include "Gvtcier2D/render/hit_rect.h"
#include "Gvtcier2D/render/menu.h"

class Renderer;
class Scene;
class Camera;

// 主菜单背景类型：2D 用一张图，3D 用一个地图场景实时渲染。
enum MainMenuBg {
    MAINMENU_BG_2D = 0,
    MAINMENU_BG_3D = 1
};

// 游戏主菜单（启动后、进入游戏前的那个界面），内嵌一个 SettingsUI。
class GVTCIER_API MainMenu {
public:
    void Gvtcier_MainMenu_SetTitle(const char* title);
    void Gvtcier_MainMenu_SetBgMode(int mode);
    int Gvtcier_MainMenu_GetBgMode() const { return bgMode_; }
    void Gvtcier_MainMenu_SetBgImage(unsigned int tex);
    void Gvtcier_MainMenu_SetBgImage(unsigned int tex, int imgW, int imgH);
    void Gvtcier_MainMenu_SetBgScene(const char* gmapPath);

    void Gvtcier_MainMenu_Open();
    void Gvtcier_MainMenu_Close();
    bool Gvtcier_MainMenu_IsOpen() const { return open_; }

    void Gvtcier_MainMenu_Tick(float dt);
    void Gvtcier_MainMenu_Update(const MenuInput& in, int winW, int winH);
    void Gvtcier_MainMenu_Draw(Renderer& r, int winW, int winH);

    // 取走"玩家点了开始游戏"并清零。
    bool Gvtcier_MainMenu_ConsumeStart();
    // 标志位：玩家选了退出，读后由上层处理。
    bool Gvtcier_MainMenu_WantsQuit() const { return wantQuit_; }
    SettingsUI& Gvtcier_MainMenu_Settings() { return settings_; }
    // 取走"设置被改动过"并清零，用于触发保存。
    bool Gvtcier_MainMenu_ConsumeSettingsChanged() {
        bool c = settingsChanged_;
        settingsChanged_ = false;
        return c;
    }
    // 0 是条目页，1 是设置页。
    int Gvtcier_MainMenu_Page() const { return page_; }

private:
    int itemAt(int mx, int my, int winW, int winH) const;

    // 条目的可点击矩形，绘制时按文字包围盒算出写回，命中判断直接读它。
    GvtHitRect itemHit_[3];

    bool open_ = true;              // 默认打开，引擎启动即显示主菜单
    bool justOpened_ = true;        // 刚打开标志，用于屏蔽首帧点击
    float lastClickTime_ = -1.0f;   // 上一次点击的时间戳，-1 表示还没有过
    int page_ = 0;                  // 0 条目页 / 1 设置页
    int sel_ = 0;                   // 选中条目，-1 表示鼠标不在条目上
    bool mouseNav_ = false;         // 当前选中是否来自鼠标
    int bgMode_ = MAINMENU_BG_2D;   // 背景类型
    bool wantQuit_ = false;         // 请求退出
    bool wantStart_ = false;        // 请求开始游戏
    bool settingsChanged_ = false;  // 设置是否被改过
    float time_ = 0.0f;             // 累计运行时间，用于点击冷却
    std::string title_;
    unsigned int bgTex_ = 0;        // 2D 背景图
    int bgTexW_ = 0, bgTexH_ = 0;   // 背景原尺寸，0 表示未知，走整屏拉伸
    std::string bgScenePath_;       // 3D 背景要加载的 gmap 路径
    SettingsUI settings_;
};
