#include "Gvtcier.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include "Gvtcier2D/render/ui_sound.h"

namespace {

// 设置页各元素的 z 值。设置面板整体压在游戏画面和抬头显示之上，
// 但低于启动画面（启动画面在 z=0/2 层单独绘制，不冲突）。
const int kZSetBg = 88;
const int kZSetText = 92;
const int kZSetSel = 90;
const int kZSetTabLine = 91;
const int kZSetCtrl = 91;

// 每行固定 46 像素高，滚动与命中都按这个常量推算，改它要同步看布局代码。
const float kRowH = 46.0f;

// 一个设置项右侧控件的可点击区域。valid=false 表示这类设置项（如纯文本说明）没有控件。
typedef GvtHitRect CtrlRect;

// 各种控件的尺寸常量，单位像素。箭头区 34 宽，开关 64x24，键位框 180x26，
// 下拉框高 28，滑条高 20（可点区域按整行算），控件总宽按 260 对齐右侧，滑条本体 200 宽。
const float kArrowW = 34.0f;
const float kToggleW = 64.0f;
const float kToggleH = 24.0f;
const float kKeycapW = 180.0f;
const float kKeycapH = 26.0f;
const float kChoiceH = 28.0f;
const float kSliderH = 20.0f;
const float kCtrlW = 260.0f;
const float kSliderW = 200.0f;

// 根据设置项类型算出它右侧控件的矩形。top 是行顶部 y，ctrlX 是控件左边界。
// 宽高与实际绘制的控件完全一致，热区不再覆盖整行，避免点在控件两侧的空白处也触发。
CtrlRect ctrlRectFor(const SettingItem& it, float top, float ctrlX) {
    CtrlRect r;
    switch (it.kind) {
        case SETTING_SLIDER:
            r = Gvtcier_Hit_RowControl(ctrlX, kSliderW + 64.0f, top, kRowH, kSliderH);
            break;
        case SETTING_TOGGLE:
            r = Gvtcier_Hit_RowControl(ctrlX, kToggleW, top, kRowH, kToggleH);
            break;
        case SETTING_CHOICE:
            r = Gvtcier_Hit_RowControl(ctrlX, kCtrlW, top, kRowH, kChoiceH);
            break;
        case SETTING_KEYCAP:
            r = Gvtcier_Hit_RowControl(ctrlX, kKeycapW, top, kRowH, kKeycapH);
            break;
        default:
            break;
    }
    return r;
}

// 鼠标点是否落在控件矩形内。相邻两行的交界处不会同时命中两行。
bool inRect(const CtrlRect& r, int mx, int my) {
    return r.contains((float)mx, (float)my);
}

// 字号倍率。标签和数值都压到 0.78 倍，让一行 46 像素能舒服放下名字加控件。
const float kLabelScale = 0.78f;
const float kValueScale = 0.78f;
const float kTitleScale = 1.15f;
const float kTabScale = 0.85f;
const float kInfoScale = 0.72f;

// 顶部区高 96 像素：标题、返回箭头、标签页都在这一带。
// 返回箭头的点击热区从 (20,18) 开始，44x44；标签页基线在 62；内容从 108 起，
// 左右各留 46 像素边距。
const float kHeaderH = 96.0f;
const float kBackX = 28.0f;
const float kBackY = kHeaderH * 0.28f;
const float kTabBarY = 62.0f;
const float kContentTop = 108.0f;
const float kContentPadX = 46.0f;

// 四个标签页的名字，索引与 tab_ 一一对应：0 通用、1 画面、2 操控、3 关于。
const char* kTabs[4] = { u8"通用", u8"画面", u8"操控", u8"关于" };

// 各下拉框的可选文案，下标即写进 GameSettings 的整数值。
const char* kOnOff[2] = { u8"关", u8"开" };
const char* kLowMidHigh[3] = { u8"低", u8"中", u8"高" };
const char* kFogOpts[4] = { u8"无", u8"低", u8"中", u8"高" };
const char* kShadowOpts[4] = { u8"关", u8"低", u8"中", u8"高" };
const char* kLightOpts[3] = { u8"低 64", u8"中 512", u8"高 2048" };
const char* kPresetOpts[3] = { u8"左手习惯", u8"右手习惯", u8"自定义" };
const char* kWindowModeOpts[4] = { u8"全屏", u8"无边框", u8"最大化", u8"窗口" };
const char* kAnisoOpts[4] = { u8"1x", u8"2x", u8"4x", u8"8x" };
const char* kFrameRateOpts[6] = { u8"垂直同步", u8"30", u8"60", u8"90", u8"120", u8"无限制" };
// 各向异性档位对应的真实倍率，写配置时要的是这个而不是下拉下标。
const int kAnisoVals[4] = { 1, 2, 4, 8 };

// 两个虚拟的"动作"编号：键位预设和菜单键都不是游戏动作，
// 但设置项结构里用 action 字段区分特殊行为，所以给它们留了 1000/1001，
// 与真实的动作序号（0..ACTION_COUNT-1）不会撞。
const int kActionKeyPreset = 1000;
const int kActionMenuKey = 1001;

// 简单格式化一个小字符串，用于把滑条数值显示成 "0.75" 这种。
std::string fmtVal(const char* f, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

}

// 按序号取标签页名，越界返回空串。
const char* SettingsUI::Gvtcier_SettingsUI_TabName(int i) const {
    return (i >= 0 && i < 4) ? kTabs[i] : "";
}

// 切换标签页。同页重复点击直接返回，避免每帧重建列表。
// 切页时会丢弃按键捕获、悬停、选中与滚动状态，因为行内容已经完全不同。
void SettingsUI::Gvtcier_SettingsUI_SetTab(int tab) {
    if (tab < 0 || tab > 3) return;
    if (tab == tab_) return;
    tab_ = tab;
    capturing_ = false;
    captureIdx_ = -1;
    hoverIdx_ = -1;
    hoverRow_ = -1;
    panel_.sel = -1;
    panel_.scrollPx = 0.0f;
    Gvtcier_SettingsUI_Build(tab_);
    panel_.sel = -1;
}

// 重建当前标签页的设置项列表。所有交互状态（捕获/拖拽/悬停）一并复位，
// 内容总高按"行高 x 行数"算，供滚动范围使用。
void SettingsUI::Gvtcier_SettingsUI_Build(int tab) {
    tab_ = tab;
    panel_.items.clear();
    panel_.scrollPx = 0.0f;
    panel_.sel = 0;
    capturing_ = false;
    captureIdx_ = -1;
    dragging_ = false;
    dragIdx_ = -1;
    hoverIdx_ = -1;
    hoverRow_ = -1;
    switch (tab) {
        case 0: buildGeneral(); break;
        case 1: buildGraphics(); break;
        case 2: buildControls(); break;
        default: buildAbout(); break;
    }
    panel_.contentH = kRowH * (float)panel_.items.size();
}

// 通用页。设置项与 GameSettings 字段的对应关系：
//   全局音量 -> masterVolume（0~1，步进 0.05）
//   音量     -> soundVolume（0~1，步进 0.05）
//   音效大小 -> musicVolume（0~1，步进 0.05）
//   运动相机 -> motionCamera（仅 3D）
//   FOV      -> fov（40~120 度，仅 3D）
//   帧率     -> frameRate（0 垂直同步 / 30 / 60 / 90 / 120 / 无限制，对应 kFrameRateOpts 下标）
//   窗口模式 -> windowMode（0 全屏 / 1 无边框 / 2 最大化 / 3 窗口）
// 纯 2D 游戏隐藏相机与 FOV，因为这两个字段在 2D 渲染里根本没用。
void SettingsUI::buildGeneral() {
    GameSettings& s = Gvtcier_Settings();
    auto add = [&](SettingItem it) { panel_.items.push_back(it); };

    SettingItem a;
    a.kind = SETTING_SLIDER; a.label = u8"全局音量";
    a.minVal = 0.0f; a.maxVal = 1.0f; a.step = 0.05f; a.floatPtr = &s.masterVolume;
    add(a);

    SettingItem b;
    b.kind = SETTING_SLIDER; b.label = u8"音量";
    b.minVal = 0.0f; b.maxVal = 1.0f; b.step = 0.05f; b.floatPtr = &s.soundVolume;
    add(b);

    SettingItem c;
    c.kind = SETTING_SLIDER; c.label = u8"音效大小";
    c.minVal = 0.0f; c.maxVal = 1.0f; c.step = 0.05f; c.floatPtr = &s.musicVolume;
    add(c);

    if (!Gvtcier_Gvtio_Is2D()) {
        SettingItem d;
        d.kind = SETTING_TOGGLE; d.label = u8"运动相机";
        d.options = kOnOff; d.optionCount = 2; d.boolPtr = &s.motionCamera;
        add(d);

        SettingItem e;
        e.kind = SETTING_SLIDER; e.label = u8"FOV";
        e.minVal = 40.0f; e.maxVal = 120.0f; e.step = 1.0f; e.floatPtr = &s.fov;
        add(e);
    }

    SettingItem f;
    f.kind = SETTING_CHOICE; f.label = u8"帧率";
    f.options = kFrameRateOpts; f.optionCount = 6; f.intPtr = &s.frameRate;
    add(f);

    SettingItem g;
    g.kind = SETTING_CHOICE; g.label = u8"窗口模式";
    g.options = kWindowModeOpts; g.optionCount = 4; g.intPtr = &s.windowMode;
    add(g);
}

// 画面页。字段对应关系：
//   抗锯齿       -> fxaa（2D 与 3D 都有）
//   HDR          -> hdr
//   Bloom 强度   -> bloomStrength（0~2）
//   各向异性过滤 -> anisotropic（存的是下拉下标，真实倍率见 kAnisoVals）
//   阴影质量     -> shadowQuality（0 关 / 1 低 / 2 中 / 3 高）
//   体积光       -> volumeLight
//   自动曝光     -> autoExposure
//   雾质量       -> fogQuality（0 无 / 1 低 / 2 中 / 3 高）
//   反光         -> specular
//   光源质量     -> lightQuality（对应 64 / 512 / 2048 三档光源数）
//   暗角         -> vignetteStrength（0~1）
//   环境光遮蔽   -> gtao
// 2D 只需要抗锯齿，其余项在 2D 下直接提前返回不显示。
void SettingsUI::buildGraphics() {
    GameSettings& s = Gvtcier_Settings();
    auto add = [&](SettingItem it) { panel_.items.push_back(it); };

    SettingItem aa;
    aa.kind = SETTING_TOGGLE; aa.label = u8"抗锯齿";
    aa.options = kOnOff; aa.optionCount = 2; aa.boolPtr = &s.fxaa;
    add(aa);
    if (Gvtcier_Gvtio_Is2D()) return;

    SettingItem a;
    a.kind = SETTING_TOGGLE; a.label = u8"HDR";
    a.options = kOnOff; a.optionCount = 2; a.boolPtr = &s.hdr;
    add(a);

    SettingItem b;
    b.kind = SETTING_SLIDER; b.label = u8"Bloom 强度";
    b.minVal = 0.0f; b.maxVal = 2.0f; b.step = 0.05f; b.floatPtr = &s.bloomStrength;
    add(b);

    SettingItem c;
    c.kind = SETTING_CHOICE; c.label = u8"各向异性过滤";
    c.options = kAnisoOpts; c.optionCount = 4; c.intPtr = &s.anisotropic;
    add(c);

    SettingItem d;
    d.kind = SETTING_CHOICE; d.label = u8"阴影质量";
    d.options = kShadowOpts; d.optionCount = 4; d.intPtr = &s.shadowQuality;
    add(d);

    SettingItem e;
    e.kind = SETTING_TOGGLE; e.label = u8"体积光";
    e.options = kOnOff; e.optionCount = 2; e.boolPtr = &s.volumeLight;
    add(e);

    SettingItem f;
    f.kind = SETTING_TOGGLE; f.label = u8"自动曝光";
    f.options = kOnOff; f.optionCount = 2; f.boolPtr = &s.autoExposure;
    add(f);

    SettingItem g;
    g.kind = SETTING_CHOICE; g.label = u8"雾质量";
    g.options = kFogOpts; g.optionCount = 4; g.intPtr = &s.fogQuality;
    add(g);

    SettingItem h;
    h.kind = SETTING_TOGGLE; h.label = u8"反光";
    h.options = kOnOff; h.optionCount = 2; h.boolPtr = &s.specular;
    add(h);

    SettingItem i;
    i.kind = SETTING_CHOICE; i.label = u8"光源质量";
    i.options = kLightOpts; i.optionCount = 3; i.intPtr = &s.lightQuality;
    add(i);

    SettingItem j;
    j.kind = SETTING_SLIDER; j.label = u8"暗角";
    j.minVal = 0.0f; j.maxVal = 1.0f; j.step = 0.05f; j.floatPtr = &s.vignetteStrength;
    add(j);

    SettingItem k;
    k.kind = SETTING_TOGGLE; k.label = u8"环境光遮蔽";
    k.options = kOnOff; k.optionCount = 2; k.boolPtr = &s.gtao;
    add(k);
}

// 操控页。分两条路：
// 2D 游戏：只列脚本通过 声明_按键 注册的按键动作（键值存在引擎的动作表里），外加菜单键（s.menuKey）。
// 3D 游戏：鼠标灵敏度（s.mouseSensitivity 0.1~3）、按键预设（s.keyPreset，0 左手/1 右手/2 自定义），
//          再接内置的 ACTION_COUNT 个动作（键值存在 s.keyBindings[]），最后是菜单键。
void SettingsUI::buildControls() {
    GameSettings& s = Gvtcier_Settings();
    auto add = [&](SettingItem it) { panel_.items.push_back(it); };

    if (Gvtcier_Gvtio_Is2D()) {
        int n = Gvtcier_Settings_ActionCount();
        for (int i = 0; i < n; ++i) {
            SettingItem k;
            k.kind = SETTING_KEYCAP;
            k.label = Gvtcier_Settings_ActionNameAt(i);
            k.action = -1;
            k.intPtr = Gvtcier_Settings_ActionVkPtrAt(i);
            add(k);
        }
        SettingItem mk;
        mk.kind = SETTING_KEYCAP;
        mk.label = u8"菜单键";
        mk.action = kActionMenuKey;
        mk.intPtr = &s.menuKey;
        add(mk);
        return;
    }

    SettingItem a;
    a.kind = SETTING_SLIDER; a.label = u8"鼠标灵敏度";
    a.minVal = 0.1f; a.maxVal = 3.0f; a.step = 0.05f; a.floatPtr = &s.mouseSensitivity;
    add(a);

    SettingItem b;
    b.kind = SETTING_CHOICE; b.label = u8"按键预设";
    b.options = kPresetOpts; b.optionCount = 3; b.intPtr = &s.keyPreset;
    b.action = kActionKeyPreset;
    add(b);

    for (int i = 0; i < ACTION_COUNT; ++i) {
        SettingItem k;
        k.kind = SETTING_KEYCAP;
        k.label = Gvtcier_Settings_ActionName(i);
        k.action = i;
        k.intPtr = &s.keyBindings[i];
        add(k);
    }

    SettingItem mk;
    mk.kind = SETTING_KEYCAP;
    mk.label = u8"菜单键";
    mk.action = kActionMenuKey;
    mk.intPtr = &s.menuKey;
    add(mk);
}

// 关于页。纯展示，没有可交互控件，SETTING_INFO 类型的行不走鼠标处理。
void SettingsUI::buildAbout() {
    SettingItem a;
    a.kind = SETTING_INFO;
    a.label = u8"Gvtcier 引擎";
    a.infoText = u8"自研 OpenGL 游戏引擎";
    panel_.items.push_back(a);

    SettingItem b;
    b.kind = SETTING_INFO;
    b.label = u8"版本";
    b.infoText = "v1.2";
    panel_.items.push_back(b);
}

// 对单个设置项做一次增量修改，delta 为 +1 或 -1。
// 开关是直接取反（delta 只用于区分"确认/跳过"，无方向含义）；下拉是循环切换，越界回绕；
// 滑条按 step 步进并夹在 [minVal,maxVal]。
// 选中"按键预设"时会立即套用预设，并把所有键位行的指针重新指向 keyBindings，
// 因为套用预设可能重建了设置结构里的键位数组。
void SettingsUI::applyChange(SettingItem& it, int delta) {
    if (it.kind == SETTING_TOGGLE && it.boolPtr) {
        *it.boolPtr = !*it.boolPtr;
    } else if (it.kind == SETTING_CHOICE && it.intPtr) {
        int n = it.optionCount;
        if (n > 0) {
            int v = *it.intPtr + delta;
            if (v < 0) v = n - 1;
            if (v >= n) v = 0;
            *it.intPtr = v;
            if (it.action == kActionKeyPreset) {
                Gvtcier_Settings_ApplyPreset(v);
                for (size_t i = 0; i < panel_.items.size(); ++i) {
                    SettingItem& sit = panel_.items[i];
                    if (sit.kind == SETTING_KEYCAP && sit.action >= 0 && sit.action < ACTION_COUNT)
                        sit.intPtr = &Gvtcier_Settings().keyBindings[sit.action];
                }
            }
        }
    } else if (it.kind == SETTING_SLIDER && it.floatPtr) {
        float v = *it.floatPtr + it.step * (float)delta;
        if (v < it.minVal) v = it.minVal;
        if (v > it.maxVal) v = it.maxVal;
        *it.floatPtr = v;
    }
}

// 设置页每帧的输入处理，返回 true 表示一帧结束（消费了输入）。
// 顺序是：返回按钮 -> 标签页 -> 逐行悬停判定 -> 键盘上下选择 -> 拖拽中 -> 点击控件 -> 按键捕获 -> 左右键微调 -> 确认键。
// outChanged 在设置值真的被改过时置 true，交给调用方保存配置；
// outBack 在点了返回箭头时置 true。
bool SettingsUI::Gvtcier_SettingsUI_Update(const MenuInput& in,
                                           int winW, int winH, bool* outChanged, bool* outBack) {
    (void)winH;
    if (outChanged) *outChanged = false;
    if (outBack) *outBack = false;

    if (in.mouseClicked) {
        if (backHit_.contains((float)in.mouseX, (float)in.mouseY)) {
            GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);
            if (outBack) *outBack = true;
            return true;
        }
    }

    mouseX_ = in.mouseX;
    mouseY_ = in.mouseY;
    hoverBack_ = backHit_.contains((float)in.mouseX, (float)in.mouseY);

    int tabHit = -1;
    if (in.mouseClicked) {
        for (int i = 0; i < 4; ++i) {
            if (tabHit_[i].contains((float)in.mouseX, (float)in.mouseY)) { tabHit = i; break; }
        }
        if (tabHit >= 0) {
            GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);
            Gvtcier_SettingsUI_SetTab(tabHit);
            return true;
        }
    }

    float ctrlX = (float)winW - kContentPadX - kCtrlW;
    float rowY0 = kContentTop - panel_.scrollPx;

    int prevHoverRow = hoverRow_;
    int prevHoverCtrl = hoverIdx_;

    int hoverRow = -1;
    int hoverCtrl = -1;
    for (size_t i = 0; i < panel_.items.size(); ++i) {
        float top = rowY0 + kRowH * (float)i;
        if ((float)in.mouseY < top || (float)in.mouseY >= top + kRowH) continue;
        if (inRect(ctrlRectFor(panel_.items[i], top, ctrlX), in.mouseX, in.mouseY))
            hoverCtrl = (int)i;
        if (labelHit_[i].contains((float)in.mouseX, (float)in.mouseY) || hoverCtrl == (int)i)
            hoverRow = (int)i;
        break;
    }
    hoverIdx_ = hoverCtrl;
    hoverRow_ = hoverRow;

    if ((hoverRow >= 0 && hoverRow != prevHoverRow) ||
        (hoverCtrl >= 0 && hoverCtrl != prevHoverCtrl)) {
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiHover);
    }

    if (!dragging_ && !capturing_) {
        if (in.mouseMoved && !in.keyNav) {
            panel_.sel = (hoverRow >= 0) ? hoverRow : -1;
        }
    }

    if (in.keyNav && !panel_.items.empty()) {
        int n = (int)panel_.items.size();
        int base = (panel_.sel >= 0) ? panel_.sel : 0;
        if (in.down) panel_.sel = (base + 1) % n;
        else if (in.up) panel_.sel = (base + n - 1) % n;
        capturing_ = false;
    }

    if (dragging_) {
        if (!in.mouseDown) {
            dragging_ = false;
            dragIdx_ = -1;
        } else if (dragIdx_ >= 0 && dragIdx_ < (int)panel_.items.size()) {
            SettingItem& it = panel_.items[dragIdx_];
            if (it.kind == SETTING_SLIDER && it.floatPtr) {
                float t = ((float)in.mouseX - ctrlX) / kSliderW;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                *it.floatPtr = it.minVal + (it.maxVal - it.minVal) * t;
                if (outChanged) *outChanged = true;
            }
        }
        return true;
    }

    if (in.mouseClicked && hoverCtrl >= 0) {
        size_t i = (size_t)hoverCtrl;
        panel_.sel = (int)i;
        SettingItem& it = panel_.items[i];
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);
        if (it.kind == SETTING_SLIDER) {
            capturing_ = false;
            dragging_ = true;
            dragIdx_ = (int)i;
            float t = ((float)in.mouseX - ctrlX) / kSliderW;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            if (it.floatPtr) *it.floatPtr = it.minVal + (it.maxVal - it.minVal) * t;
            if (outChanged) *outChanged = true;
        } else if (it.kind == SETTING_KEYCAP) {
            capturing_ = true;
            captureIdx_ = (int)i;
        } else if (it.kind == SETTING_CHOICE && it.intPtr) {
            capturing_ = false;
            if ((float)in.mouseX < ctrlX + kArrowW) {
                applyChange(it, -1);
            } else if ((float)in.mouseX > ctrlX + kCtrlW - kArrowW) {
                applyChange(it, 1);
            }
            if (outChanged) *outChanged = true;
        } else if (it.kind == SETTING_TOGGLE) {
            capturing_ = false;
            applyChange(it, 1);
            if (outChanged) *outChanged = true;
        }
    }

    SettingItem* cur = (panel_.sel >= 0 && panel_.sel < (int)panel_.items.size())
                       ? &panel_.items[panel_.sel] : nullptr;

    if (capturing_) {
        if (in.capturedKey != 0) {
            if (captureIdx_ >= 0 && captureIdx_ < (int)panel_.items.size()) {
                SettingItem& it = panel_.items[captureIdx_];
                if (it.intPtr) {
                    *it.intPtr = (in.capturedKey == 0x1B) ? 0 : in.capturedKey;
                }
                if (outChanged) *outChanged = true;
            }
            capturing_ = false;
            captureIdx_ = -1;
        }
        return true;
    }

    if (cur && (in.left || in.right)) {
        if (cur->kind == SETTING_INFO) return true;
        int d = in.right ? 1 : -1;
        if (cur->kind == SETTING_TOGGLE) {
            applyChange(*cur, d);
        } else if (cur->kind == SETTING_CHOICE || cur->kind == SETTING_SLIDER) {
            applyChange(*cur, d);
        }
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);
        if (outChanged) *outChanged = true;
    }

    if (cur && in.confirm) {
        if (cur->kind == SETTING_KEYCAP) {
            capturing_ = true;
            captureIdx_ = panel_.sel;
        } else if (cur->kind == SETTING_TOGGLE || cur->kind == SETTING_CHOICE) {
            applyChange(*cur, 1);
            if (outChanged) *outChanged = true;
        }
        GvtUiSound::Gvtcier_UiSound_Play(GvtUiSound::UiPress);
    }

    return true;
}

// 绘制整个设置界面：全屏半透明底色、返回箭头、居中标题、四个标签页及其选中下划线，
// 然后按滚动偏移逐行画设置项。屏幕外的行直接跳过，避免无谓的文本测量。
// 注意 z 值：背景 88 最低，选中条/标签线 90/91，控件 91，文字 92 最高。
void SettingsUI::Gvtcier_SettingsUI_Draw(Renderer& r, int winW, int winH) {
    r.Gvtcier_Render_DrawRect2D(0.0f, 0.0f, (float)winW, (float)winH, 0.07f, 0.08f, 0.10f, 0.90f, kZSetBg);

    backHit_ = Gvtcier_Hit_DrawText(r, kBackX, kBackY, kTitleScale, "<", kZSetText).inflated(10.0f);
    if (hoverBack_)
        r.Gvtcier_Render_DrawRect2D(backHit_.x, backHit_.y,
                                    backHit_.w, backHit_.h, 0.22f, 0.26f, 0.32f, 0.85f, kZSetSel);

    float titleW = 0.0f, titleH = 0.0f, titleTop = 0.0f;
    r.Gvtcier_Render_MeasureTextEx(u8"设置", kTitleScale, &titleW, &titleH, &titleTop);
    r.Gvtcier_Render_DrawText((int)(((float)winW - titleW) * 0.5f),
                              (int)(kHeaderH * 0.28f - titleTop), kTitleScale, u8"设置", kZSetText);

    float tabW = 132.0f;
    float totalW = tabW * 4.0f;
    float startX = ((float)winW - totalW) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        float tx = startX + tabW * (float)i + 8.0f;
        tabHit_[i] = Gvtcier_Hit_DrawText(r, tx, kTabBarY, kTabScale, kTabs[i], kZSetText).inflated(8.0f, 6.0f);
        if (i == tab_) {
            r.Gvtcier_Render_DrawRect2D(tabHit_[i].x, tabHit_[i].bottom() + 2.0f, tabHit_[i].w, 2.0f,
                                        0.35f, 0.75f, 1.0f, 0.95f, kZSetTabLine);
        }
    }

    float ctrlX = (float)winW - kContentPadX - kCtrlW;
    float rowY = kContentTop - panel_.scrollPx;
    labelHit_.assign(panel_.items.size(), GvtHitRect());
    for (size_t i = 0; i < panel_.items.size(); ++i) {
        SettingItem& it = panel_.items[i];
        float top = rowY + kRowH * (float)i;
        if (top + kRowH < kContentTop || top > (float)winH) continue;
        bool sel = ((int)i == panel_.sel);
        bool hov = ((int)i == hoverIdx_);

        if (sel && it.kind != SETTING_INFO)
            r.Gvtcier_Render_DrawRect2D(kContentPadX - 14.0f, top + 4.0f,
                                        (float)winW - kContentPadX * 2.0f + 28.0f, kRowH - 8.0f,
                                        0.16f, 0.18f, 0.22f, 0.85f, kZSetSel);
        if (sel && it.kind != SETTING_INFO)
            r.Gvtcier_Render_DrawRect2D(kContentPadX - 14.0f, top + 4.0f, 3.0f, kRowH - 8.0f,
                                        0.35f, 0.75f, 1.0f, 0.95f, kZSetSel);

        float lw = 0.0f, lh = 0.0f, ltop = 0.0f;
        r.Gvtcier_Render_MeasureTextEx(it.label, kLabelScale, &lw, &lh, &ltop);
        float textY = top + (kRowH - lh) * 0.5f - ltop;
        labelHit_[i] = Gvtcier_Hit_DrawText(r, kContentPadX, textY, kLabelScale, it.label, kZSetText);

        if (it.kind == SETTING_INFO) continue;

        CtrlRect box = ctrlRectFor(it, top, ctrlX);

        if (it.kind == SETTING_SLIDER && it.floatPtr) {
            float t = (it.maxVal > it.minVal) ? (*it.floatPtr - it.minVal) / (it.maxVal - it.minVal) : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float sy = box.midY() - 3.0f;
            r.Gvtcier_Render_DrawRect2D(ctrlX, sy, kSliderW, 6.0f, 0.22f, 0.24f, 0.28f, 0.95f, kZSetCtrl);
            r.Gvtcier_Render_DrawRect2D(ctrlX, sy, kSliderW * t, 6.0f, 0.35f, 0.75f, 1.0f, 0.95f, kZSetCtrl);
            r.Gvtcier_Render_DrawRect2D(ctrlX + kSliderW * t - 5.0f, sy - 5.0f, 10.0f, 16.0f,
                                        0.85f, 0.88f, 0.92f, 1.0f, kZSetCtrl);
            std::string v = fmtVal("%.2f", *it.floatPtr);
            Gvtcier_Hit_DrawText(r, ctrlX + kSliderW + 20.0f, box.y, kValueScale, v.c_str(), kZSetText);
        } else if (it.kind == SETTING_TOGGLE && it.boolPtr) {
            const char* v = *it.boolPtr ? kOnOff[1] : kOnOff[0];
            float lift = hov ? 0.06f : 0.0f;
            float on = *it.boolPtr ? 1.0f : 0.0f;
            r.Gvtcier_Render_DrawRect2D(box.x, box.y, box.w, box.h,
                                        (on > 0.5f ? 0.20f : 0.22f) + lift,
                                        (on > 0.5f ? 0.48f : 0.24f) + lift,
                                        (on > 0.5f ? 0.70f : 0.28f) + lift, 0.95f, kZSetCtrl);
            Gvtcier_Hit_DrawTextCentered(r, box.midX(), box.y, kValueScale, v, kZSetText);
        } else if (it.kind == SETTING_CHOICE && it.intPtr && it.optionCount > 0) {
            float arrowW = kArrowW;
            const char* curtxt = (it.options && *it.intPtr >= 0 && *it.intPtr < it.optionCount)
                                 ? it.options[*it.intPtr] : "";
            float boxX = box.x + arrowW;
            float boxW = box.w - arrowW * 2.0f;
            float lift = hov ? 0.05f : 0.0f;
            bool hoverLeft = hov && (float)mouseX_ < box.x + arrowW;
            bool hoverRight = hov && (float)mouseX_ > box.x + box.w - arrowW;
            r.Gvtcier_Render_DrawRect2D(box.x, box.y, arrowW, box.h,
                                        0.18f + lift + (hoverLeft ? 0.10f : 0.0f),
                                        0.20f + lift + (hoverLeft ? 0.14f : 0.0f),
                                        0.24f + lift + (hoverLeft ? 0.18f : 0.0f), 0.95f, kZSetCtrl);
            r.Gvtcier_Render_DrawRect2D(box.x + box.w - arrowW, box.y, arrowW, box.h,
                                        0.18f + lift + (hoverRight ? 0.10f : 0.0f),
                                        0.20f + lift + (hoverRight ? 0.14f : 0.0f),
                                        0.24f + lift + (hoverRight ? 0.18f : 0.0f), 0.95f, kZSetCtrl);
            Gvtcier_Hit_DrawTextCentered(r, box.x + arrowW * 0.5f, box.y, kValueScale, "<", kZSetText);
            Gvtcier_Hit_DrawTextCentered(r, box.x + box.w - arrowW * 0.5f, box.y, kValueScale, ">", kZSetText);

            r.Gvtcier_Render_DrawRect2D(boxX, box.y, boxW, box.h, 0.13f + lift, 0.14f + lift, 0.17f + lift, 0.95f, kZSetCtrl);
            Gvtcier_Hit_DrawTextCentered(r, boxX + boxW * 0.5f, box.y, kValueScale, curtxt, kZSetText);
        } else if (it.kind == SETTING_KEYCAP && it.intPtr) {
            bool isCap = capturing_ && captureIdx_ == (int)i;
            std::string v = isCap ? u8"按任意键..." : Gvtcier_Settings_KeyName(*it.intPtr);
            float lift = (hov && !isCap) ? 0.06f : 0.0f;
            r.Gvtcier_Render_DrawRect2D(box.x, box.y, box.w, box.h,
                                        (isCap ? 0.30f : 0.16f) + lift,
                                        (isCap ? 0.42f : 0.17f) + lift,
                                        (isCap ? 0.62f : 0.20f) + lift, 0.95f, kZSetCtrl);
            Gvtcier_Hit_DrawTextCentered(r, box.midX(), box.y, kValueScale, v.c_str(), kZSetText);
        }
    }
}
