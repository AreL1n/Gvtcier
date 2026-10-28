#pragma once
#include <string>
#include <vector>
#include "bicu/core/export.h"
#include "Gvtcier2D/render/hit_rect.h"

class Renderer;
struct GameSettings;

// 设置项的类型，决定右侧画什么控件、怎么响应输入。
enum SettingKind {
    SETTING_TOGGLE = 0,
    SETTING_SLIDER,
    SETTING_CHOICE,
    SETTING_KEYCAP,
    SETTING_INFO
};

// 一个设置行。控件直接操作目标指针（intPtr/floatPtr/boolPtr 指向 GameSettings 里的字段），
// 所以改值不需要额外回写步骤，但指针必须在面板存活期间保持有效。
struct SettingItem {
    SettingKind kind = SETTING_TOGGLE;
    const char* label = "";
    const char* const* options = nullptr;   // 下拉框选项文案数组
    int optionCount = 0;
    float minVal = 0.0f;                    // 滑条范围
    float maxVal = 1.0f;
    float step = 0.1f;                      // 滑条步进与左右键微调量
    int action = -1;                        // 键位行对应的动作序号；-1 表示非动作行
    int* intPtr = nullptr;                  // 下拉/键位的目标
    float* floatPtr = nullptr;              // 滑条的目标
    bool* boolPtr = nullptr;                // 开关的目标
    const char* infoText = nullptr;         // SETTING_INFO 的展示文本
};

// 一个标签页的全部行，加上滚动位置与键盘选中行。
struct SettingPanel {
    std::vector<SettingItem> items;
    float scrollPx = 0.0f;                  // 已滚动的像素，行 y 都要减掉它
    float contentH = 0.0f;                  // 内容总高，用于算滚动上限
    int sel = 0;                            // 键盘选中行，-1 表示无选中
};

// 设置界面：四个标签页（通用/画面/操控/关于），同时支持鼠标与键盘操作。
class GVTCIER_API SettingsUI {
public:
    // 重建指定标签页的行列表。
    void Gvtcier_SettingsUI_Build(int tab);

    int Gvtcier_SettingsUI_TabCount() const { return 4; }
    const char* Gvtcier_SettingsUI_TabName(int i) const;
    int Gvtcier_SettingsUI_Tab() const { return tab_; }
    void Gvtcier_SettingsUI_SetTab(int tab);

    // 每帧输入处理。outChanged 报告设置是否被改动，outBack 报告是否点了返回。
    bool Gvtcier_SettingsUI_Update(const struct MenuInput& in, int winW, int winH,
                                   bool* outChanged, bool* outBack);
    // 绘制整个界面。
    void Gvtcier_SettingsUI_Draw(class Renderer& r, int winW, int winH);

    // 临时离开页面时清掉捕获/拖拽，避免回来后还卡在"按任意键"状态。
    void Gvtcier_SettingsUI_ResetFocus() { capturing_ = false; captureIdx_ = -1; dragging_ = false; dragIdx_ = -1; }

private:
    void buildGeneral();
    void buildGraphics();
    void buildControls();
    void buildAbout();
    void applyChange(SettingItem& it, int delta);

    int tab_ = 0;              // 当前标签页 0~3
    bool capturing_ = false;   // 正在等待玩家按下一个键来绑定
    int captureIdx_ = -1;      // 处于捕获状态的行下标
    bool dragging_ = false;    // 正在拖动滑条
    int dragIdx_ = -1;         // 被拖动滑条的行下标
    int hoverIdx_ = -1;        // 鼠标悬停的控件行，-1 无
    int hoverRow_ = -1;        // 鼠标悬停的行（标签文字或控件命中），-1 无
    // 每行标签文字的可点击矩形，绘制时算出写回，命中判断直接读它。
    std::vector<GvtHitRect> labelHit_;
    int mouseX_ = 0;           // 最近一次鼠标位置，绘制时用来判断箭头是否悬停
    int mouseY_ = 0;
    bool hoverBack_ = false;   // 返回箭头是否悬停
    GvtHitRect backHit_;       // 返回箭头的可点击矩形，绘制时算出写回
    GvtHitRect tabHit_[4];     // 四个标签页各自的可点击矩形，绘制时算出写回
    SettingPanel panel_;       // 当前页的行数据
};
