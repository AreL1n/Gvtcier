#pragma once
#include "bicu/core/export.h"
#include "bicu/core/identity.h"
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>

// 锚点用千分数表示：0 父矩形左/上边，500 中心，1000 右/下边。
enum { kUiAnchorBegin = 0, kUiAnchorCenter = 500, kUiAnchorEnd = 1000 };

// 控件类型。绘制时按类型选取主题里的样式组。
enum UiKind {
    kUiContainer,
    kUiCard,
    kUiButton,
    kUiLabel,
};

// 一个控件的视觉样式。边框、圆角、投影三件套，外加是否填充中心。
struct IfStyleBox {
    COLORREF bg = RGB(0xCE, 0xDF, 0xEB);
    COLORREF border = RGB(0x9A, 0xB2, 0xC4);
    int borderW = 1;
    int radius = 8;
    int shadowSize = 0;                                     // 投影扩散半径，0 表示不画投影
    COLORREF shadowColor = RGB(0x00, 0x00, 0x00);
    bool drawCenter = true;                                 // false 时只描边不填充
};

// 一个 UI 对象。位置由"锚点（千分数）+ 偏移（像素）"决定，Layout 时算出最终的 rect。
struct UiObject {
    int id = 0;                    // 内部自增序号，仅用于调试与排序，不参与 ID 系统查表
    UiKind kind = kUiContainer;
    bool visible = true;
    GvtId<UiObject> parent;        // 父对象 ID，无效表示挂在根下
    int z = 0;                     // 绘制层级，越小越先画
    int anchor[4] = { kUiAnchorBegin, kUiAnchorBegin, kUiAnchorBegin, kUiAnchorBegin }; // 左上右下四角锚点
    int offset[4] = { 0, 0, 0, 0 }; // 四角在锚点基础上的像素偏移
    RECT rect = { 0, 0, 0, 0 };    // Layout 算出的最终像素矩形
    IfStyleBox style;              // 自定义样式，仅 customStyle 为真时生效
    bool customStyle = false;
    int fitDir = 0;                // 0 不平分；1 水平均分给子级；2 垂直均分
    std::string text;
    COLORREF textColor = RGB(0x14, 0x28, 0x38);
    UINT textFmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
    bool hover = false, pressed = false, disabled = false;  // 交互状态，由调用方设置
    const char* type = nullptr;    // 覆盖主题里的类型名，为空时按 kind 推导
    void* user = nullptr;          // 使用方自由挂数据
};

// 主题：两层字典，类型名 -> 状态名 -> 样式 / 颜色。
struct GVTCIER_API IfTheme {
    std::unordered_map<std::string, std::unordered_map<std::string, IfStyleBox>> styles;
    std::unordered_map<std::string, std::unordered_map<std::string, COLORREF>> colors;

    void Gvtcier_UI_SetStyle(const char* type, const char* name, const IfStyleBox& sb);
    const IfStyleBox* Gvtcier_UI_GetStyle(const char* type, const char* name) const;
    IfStyleBox* Gvtcier_UI_GetStyle(const char* type, const char* name);
    void Gvtcier_UI_SetColor(const char* type, const char* name, COLORREF c);
    // 取颜色，缺失时返回 fallback。
    COLORREF Gvtcier_UI_GetColor(const char* type, const char* name, COLORREF fallback) const;
    // 灌入内置默认主题。
    void Gvtcier_UI_InitDefault();
};

// UI 画布：持有全部 UI 对象（走引擎 ID 系统），负责布局、绘制、命中测试。
// 这不是 2D 画布（canvas2d），而是基于 GDI 的原生界面树，通常用于编辑器类窗口。
class GVTCIER_API UiCanvas {
public:
    Gvtcier_Object<UiObject> objs_;
    IfTheme theme;                 // 本画布独立的主题
    int nextId_ = 0;               // 自增序号

    // 以像素矩形添加对象。
    GvtId<UiObject> Gvtcier_UI_Add(UiKind kind, const char* text, GvtId<UiObject> parent, int z,
            int x, int y, int w, int h, GvtId<UiObject>* idOut = nullptr);
    // 以锚点 + 偏移添加对象（自适应布局）。
    GvtId<UiObject> Gvtcier_UI_AddAnchor(UiKind kind, const char* text, GvtId<UiObject> parent, int z,
                  int aL, int aT, int aR, int aB,
                  int oL, int oT, int oR, int oB, GvtId<UiObject>* idOut = nullptr);
    UiObject* Gvtcier_UI_Find(GvtId<UiObject> id);
    const UiObject* Gvtcier_UI_Find(GvtId<UiObject> id) const;
    void Gvtcier_UI_Remove(GvtId<UiObject> id);
    void Gvtcier_UI_Clear();
    void Gvtcier_UI_SetText(GvtId<UiObject> id, const char* text);
    void Gvtcier_UI_SetRect(GvtId<UiObject> id, int x, int y, int w, int h);
    void Gvtcier_UI_Move(GvtId<UiObject> id, int dx, int dy);
    void Gvtcier_UI_SetVisible(GvtId<UiObject> id, bool v);
    void Gvtcier_UI_SetStyle(GvtId<UiObject> id, const IfStyleBox& sb);

    // 按根矩形重算全部对象位置（迭代到收敛）。
    void Gvtcier_UI_Layout(const RECT& root);
    void Gvtcier_UI_Draw(HDC hdc, const RECT& root);
    // 返回鼠标下 z 最大的可见对象。
    GvtId<UiObject> Gvtcier_UI_Hit(int x, int y) const;

private:
    RECT rootRect_ = { 0, 0, 0, 0 };  // 最近一次 Layout 的根矩形
    void fitChildren();
};

// 竖向滚动面板辅助。Begin -> 若干 Claim/Reserve -> End 一套流程算内容高与滚动量，
// 滚动状态按窗口句柄存在实现文件的全局表里。
struct GVTCIER_API GvPanel {
    HWND wnd = nullptr;
    int scrollY = 0;        // 当前滚动偏移（像素）
    int contentH = 0;       // 上一轮算出内容总高

    void Gvtcier_UI_Panel_Attach(HWND hwnd);
    void Gvtcier_UI_Panel_Begin();
    // 声明一块内容，恒返回 true。
    bool Gvtcier_UI_Panel_Claim(int x, int y, int w, int h);
    void Gvtcier_UI_Panel_Reserve(int h);
    // 收尾：更新内容高与滚动条。
    void Gvtcier_UI_Panel_End(int contentBottom);
    void Gvtcier_UI_Panel_Scroll(int delta);
    void Gvtcier_UI_Panel_SuspendDraw();
    void Gvtcier_UI_Panel_ResumeDraw();
    // 绘制子控件时要把 y 减去这个偏移。
    int Gvtcier_UI_Panel_Offset() const { return scrollY; }
};
