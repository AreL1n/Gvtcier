#include "Gvtcier.h"
#include <windows.h>
#include <imm.h>
#include <richedit.h>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <algorithm>
#include <vector>

namespace {

// 从窗口句柄取回 Window 对象。GvW 创建窗口时把 Window 指针塞进 GWLP_USERDATA，
// 这里只是反向取出来，供回调使用。
inline Window* windowFromHwnd(HWND h) {
    return (Window*)GetWindowLongPtrA(h, GWLP_USERDATA);
}

// 用逐行画横线的方式模拟竖直渐变。GDI 没有现成的渐变填充 API，
// 每行算一次插值颜色、建一支单像素笔再画一条线，几千行时开销不小，
// 所以只用在标题栏、视口顶栏这类小面积区域。
void fillGradientV(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom) {
    int h = rc.bottom - rc.top;
    if (h <= 0) return;
    int r1 = GetRValue(top), g1 = GetGValue(top), b1 = GetBValue(top);
    int r2 = GetRValue(bottom), g2 = GetGValue(bottom), b2 = GetBValue(bottom);
    for (int y = 0; y < h; ++y) {
        int t = y * 256 / h;
        int r = r1 + (r2 - r1) * t / 256;
        int g = g1 + (g2 - g1) * t / 256;
        int b = b1 + (b2 - b1) * t / 256;
        HPEN pn = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN op = (HPEN)SelectObject(hdc, pn);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        MoveToEx(hdc, rc.left, rc.top + y, nullptr);
        LineTo(hdc, rc.right, rc.top + y);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pn);
    }
}
}

// 窗口尺寸变化的桥接函数：GvW 窗口层不直接依赖 RHI，所以在这里把通知转发给渲染后端，
// 让视口与后台缓冲跟着窗口一起变。
static void gvwResizeBridge(int w, int h) {
    rhi::Gvtcier_Rhi_Resize(w, h);
}

// 创建窗口。parent 非空时创建子窗口（嵌入编辑器面板用），否则是顶层窗口。
// 桥接回调只在第一次调用时安装，用静态标志防止重复注册。
bool Window::Gvtcier_Window_Create(const char* title, int w, int h, HWND parent) {
    static bool bridgeInstalled = false;
    if (!bridgeInstalled) {
        GvW_SetResizeCallback(gvwResizeBridge);
        bridgeInstalled = true;
    }
    bool ok = parent ? GvW_CreateIn(&gw_, title, w, h, parent)
                     : GvW_Create(&gw_, title, w, h);
    if (ok) printf("[window] %dx%d window created\n", GvW_Width(&gw_), GvW_Height(&gw_));
    else printf("[window] CreateWindow failed\n");
    return ok;
}

// 应用窗口模式（全屏/无边框/最大化/窗口），具体切换逻辑在 GvW 里。
void Window::Gvtcier_Window_ApplyMode(int mode) { GvW_ApplyMode(&gw_, mode); }

// 销毁窗口。必须先关渲染后端再销毁 HWND，否则 GL 上下文会在窗口消失后还活着。
void Window::Gvtcier_Window_Destroy() {
    rhi::Gvtcier_Rhi_Shutdown();
    GvW_Destroy(&gw_);
}

// 处理一轮窗口消息，返回 false 表示收到了退出消息，主循环应当结束。
bool Window::Gvtcier_Window_Poll() {
    return GvW_Poll(&gw_);
}

// 提交后台缓冲并交换前后缓冲，一帧画面在这一步真正显示。
void Window::Gvtcier_Window_SwapBuffers() {
    rhi::Gvtcier_Rhi_Present();
}

// 取走本帧累积的滚轮增量后清零，正数向上、负数向下。
int Window::Gvtcier_Window_ConsumeScroll() {
    return GvW_ConsumeScroll(&gw_);
}

// 强制释放所有按键的按下状态。窗口失焦时必须调，否则 Alt+Tab 离开时按住的键会卡住。
void Window::Gvtcier_Window_ReleaseAllKeys() {
    GvW_ReleaseAllKeys(&gw_);
}

// 捕获/释放鼠标。捕获时光标被锁在窗口内并隐藏，3D 视角控制需要；界面上则要释放。
bool Window::Gvtcier_Window_GrabMouse(bool grab) {
    return GvW_GrabMouse(&gw_, grab);
}

// 根据当前窗口状态刷新裁剪区与光标隐藏状态。每帧调用以适配全屏/窗口切换。
void Window::Gvtcier_Window_UpdateClipCursorHide() {
    GvW_UpdateClipCursorHide(&gw_);
}

// 恢复系统光标显示，退出游戏或弹菜单前调用。
void Window::Gvtcier_Window_RestoreCursor() {
    GvW_RestoreCursor(&gw_);
}

// 开关键盘输入。关闭时窗口层不再累积按键状态，适合播放过场动画或失焦时用。
void Window::Gvtcier_Window_SetInputEnabled(bool enabled) {
    GvW_SetInputEnabled(&gw_, enabled);
}

// 取走一个"本帧新按下的键"的虚拟键码，没有则返回 0。用于"按任意键继续"之类的场景。
int Window::Gvtcier_Window_ConsumeKeyPress() {
    return GvW_ConsumeKeyPress(&gw_);
}

// 开关自动捕获鼠标。开启后进入游戏自动抓鼠标，退出时自动放开。
void Window::Gvtcier_Window_SetAutoGrab(bool on) {
    GvW_SetAutoGrab(&gw_, on);
}

// 读取鼠标在客户区内的像素坐标。
void Window::Gvtcier_Window_CursorPos(int& x, int& y) const {
    GvW_MousePos(&gw_, &x, &y);
}

// 窗口是否为前台激活窗口，用来决定要不要处理输入。
bool Window::Gvtcier_Window_IsForeground() const {
    return GvW_IsForeground(&gw_);
}

// 读取并清零这一帧的鼠标相对位移，鼠标捕获状态下做 yaw/pitch 用。
void Window::Gvtcier_Window_MouseDelta(int& dx, int& dy) {
    GvW_MouseDelta(&gw_, &dx, &dy);
}

// 原生控件（按钮/标签/编辑框）用的 UI 字体：
// g_uiFont 常规体、g_uiFontBold 粗体、g_uiFontSize 记录字号、
// g_uiFontMem 保存内存字体注册句柄，释放时要逐个注销。
static HFONT g_uiFont = nullptr;
static HFONT g_uiFontBold = nullptr;
static int g_uiFontSize = 16;
static HANDLE g_uiFontMem[4] = { nullptr, nullptr, nullptr, nullptr };
static int g_uiFontMemCount = 0;

// 实心刷子缓存。CreateSolidBrush 每次都要向 GDI 申请资源，而界面配色就那么几十种，
// 按颜色值缓存起来复用，统一在 Gvtcier_UI_ReleaseBrushes 里释放。
static std::unordered_map<COLORREF, HBRUSH> g_Gvtcier_UI_Brushes;

// 加载原生控件字体。regularRes/boldRes 是资源段里的字体名，familyName 是系统字体族名。
// 先释放旧字体再建新的；建失败就退回系统默认 GUI 字体，保证界面还能显示。
// 返回字体是否可用。
bool Gvtcier_UI_LoadFont(const char* regularRes, const char* boldRes, const wchar_t* familyName, int px) {
    Gvtcier_UI_ReleaseFont();
    g_uiFontSize = px;
    int sz = 0;
    if (regularRes && regularRes[0]) {
        const void* data = res_get(regularRes, &sz);
        if (data && sz > 0) {
            DWORD n = 0;
            HANDLE hf = AddFontMemResourceEx((void*)data, (DWORD)sz, nullptr, &n);
            if (hf) g_uiFontMem[g_uiFontMemCount++] = hf;
        }
    }
    if (boldRes && boldRes[0]) {
        sz = 0;
        const void* data = res_get(boldRes, &sz);
        if (data && sz > 0) {
            DWORD n = 0;
            HANDLE hf = AddFontMemResourceEx((void*)data, (DWORD)sz, nullptr, &n);
            if (hf && g_uiFontMemCount < 4) g_uiFontMem[g_uiFontMemCount++] = hf;
        }
    }
    g_uiFont = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, familyName);
    g_uiFontBold = CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, familyName);
    if (!g_uiFont) g_uiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_uiFontBold) g_uiFontBold = g_uiFont;
    return g_uiFont != nullptr;
}

// 取当前常规体字体句柄，供 SendMessage(WM_SETFONT) 使用。可能为空。
HFONT Gvtcier_UI_Font() {
    return g_uiFont;
}

// 释放 UI 字体。默认 GUI 字体是系统共享对象，不能删，所以用比较句柄的方式排除掉；
// 粗体若与常规体是同一个句柄也不能重复删。
void Gvtcier_UI_ReleaseFont() {
    if (g_uiFont && g_uiFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(g_uiFont);
    if (g_uiFontBold && g_uiFontBold != g_uiFont && g_uiFontBold != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(g_uiFontBold);
    g_uiFont = nullptr;
    g_uiFontBold = nullptr;
    for (int i = 0; i < g_uiFontMemCount; ++i) {
        if (g_uiFontMem[i]) {
            RemoveFontMemResourceEx(g_uiFontMem[i]);
            g_uiFontMem[i] = nullptr;
        }
    }
    g_uiFontMemCount = 0;
}

// 释放所有缓存的实心刷子。界面关闭或换主题时调用，之后会重新按需创建。
void Gvtcier_UI_ReleaseBrushes() {
    for (auto& kv : g_Gvtcier_UI_Brushes) DeleteObject(kv.second);
    g_Gvtcier_UI_Brushes.clear();
}

// 给窗口本身以及它所有子控件统一套字体。用 EnumChildWindows 递归覆盖，
// 这样新加控件不用逐个设置字体。
void Gvtcier_UI_ApplyFont(HWND hwnd, HFONT font) {
    if (!font) return;
    SendMessageA(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL {
        SendMessageA(c, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)font);
}

// 把常规体选进 DC，后续 DrawText 都用它。GDI 的"选入"是隐式的，用完不需要显式还原。
void Gvtcier_UI_SelectFont(HDC hdc) {
    if (g_uiFont) SelectObject(hdc, g_uiFont);
}

// 把粗体选进 DC，标题栏、表头这类强调文字用。
void Gvtcier_UI_SelectFontBold(HDC hdc) {
    if (g_uiFontBold) SelectObject(hdc, g_uiFontBold);
}

// 检查系统当前代码页是否为 UTF-8。不是的话窗口层的中文会走 ANSI 转换，可能乱码，
// 所以启动时用它做一次体检。
bool Gvtcier_UI_EnsureUtf8() {
    return Gvtcier_W32_IsUtf8CodePage();
}

// UTF-8 转宽字符串，所有给 Win32 W 系列 API 的文本都要先过这一层。
std::wstring Gvtcier_UI_W(const char* utf8) {
    return Gvtcier_W32_ToWide(utf8);
}

// 读控件文本并按 UTF-8 拷进调用方缓冲。转换后超过 len-1 的部分直接截断，始终保证以 0 结尾。
void Gvtcier_UI_GetText(HWND hwnd, char* buf, int len) {
    if (!buf || len <= 0) return;
    std::string s = Gvtcier_W32_GetWindowTextUtf8(hwnd);
    if (s.empty()) { buf[0] = 0; return; }
    size_t n = s.size();
    if (n > (size_t)(len - 1)) n = (size_t)(len - 1);
    memcpy(buf, s.data(), n);
    buf[n] = 0;
}

// 创建原生子控件的统一入口：一律 WS_CHILD | WS_VISIBLE，再叠加各自风格的 style，
// 建完立刻套上 UI 常规体。所有 Button/Label/Edit/Listbox 都走这里。
static HWND ifaceCreateW(const wchar_t* cls, const std::wstring& text, DWORD style,
                         int x, int y, int w, int h, HWND parent, int id) {
    HWND hw = CreateWindowW(cls, text.c_str(), WS_CHILD | WS_VISIBLE | style,
                            x, y, w, h, parent, (HMENU)(INT_PTR)id,
                            GetModuleHandleW(NULL), nullptr);
    if (g_uiFont) SendMessageW(hw, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
    return hw;
}

// 各控件的便捷创建函数。text 一律按 UTF-8 传入，内部转宽字符；
// id 是控件的消息 id，父窗口用它区分 WM_COMMAND 来自哪个控件。
HWND Gvtcier_UI_Button(HWND parent, const char* text, DWORD style, int x, int y, int w, int h, int id) {
    return ifaceCreateW(L"BUTTON", Gvtcier_UI_W(text), style, x, y, w, h, parent, id);
}

HWND Gvtcier_UI_Label(HWND parent, const char* text, int x, int y, int w, int h, int id) {
    return ifaceCreateW(L"STATIC", Gvtcier_UI_W(text), 0, x, y, w, h, parent, id);
}

HWND Gvtcier_UI_Edit(HWND parent, const char* text, DWORD style, int x, int y, int w, int h, int id) {
    return ifaceCreateW(L"EDIT", Gvtcier_UI_W(text), style, x, y, w, h, parent, id);
}

HWND Gvtcier_UI_Listbox(HWND parent, DWORD style, int x, int y, int w, int h, int id) {
    return ifaceCreateW(L"LISTBOX", std::wstring(), style, x, y, w, h, parent, id);
}

// 往菜单尾部追加一项，flags 决定是普通项、分隔线还是勾选/禁用状态。
void Gvtcier_UI_MenuAppend(HMENU m, UINT flags, UINT_PTR id, const char* text) {
    AppendMenuW(m, flags, id, Gvtcier_UI_W(text).c_str());
}

const COLORREF Gvtcier_UI_Bg          = RGB(0x1E, 0x21, 0x27);
const COLORREF Gvtcier_UI_BgPanel     = RGB(0x25, 0x28, 0x2E);
const COLORREF Gvtcier_UI_BgCard      = RGB(0x2D, 0x30, 0x38);
const COLORREF Gvtcier_UI_BgToolbar   = RGB(0x18, 0x1A, 0x1F);
const COLORREF Gvtcier_UI_BgStatusbar = RGB(0x14, 0x16, 0x1A);
const COLORREF Gvtcier_UI_BgTitlebar  = RGB(0x0E, 0x10, 0x14);
const COLORREF Gvtcier_UI_BgInput     = RGB(0x1A, 0x1D, 0x22);
const COLORREF Gvtcier_UI_Item        = RGB(0x25, 0x28, 0x2E);
const COLORREF Gvtcier_UI_ItemHover   = RGB(0x33, 0x37, 0x3F);
const COLORREF Gvtcier_UI_ItemSel     = RGB(0x4A, 0x6C, 0xF7);
const COLORREF Gvtcier_UI_ItemSelBg   = RGB(0x2E, 0x39, 0x55);
const COLORREF Gvtcier_UI_Border      = RGB(0x3A, 0x3D, 0x44);
const COLORREF Gvtcier_UI_BorderSubtle= RGB(0x2A, 0x2D, 0x32);
const COLORREF Gvtcier_UI_BorderStrong= RGB(0x4E, 0x52, 0x5A);
const COLORREF Gvtcier_UI_BorderSel   = RGB(0x4A, 0x6C, 0xF7);
const COLORREF Gvtcier_UI_TextColor  = RGB(0xE6, 0xE8, 0xEB);
const COLORREF Gvtcier_UI_TextMuted   = RGB(0x9D, 0xA0, 0xA6);
const COLORREF Gvtcier_UI_TextDim     = RGB(0x6C, 0x6F, 0x76);
const COLORREF Gvtcier_UI_Accent      = RGB(0x4A, 0x6C, 0xF7);
const COLORREF Gvtcier_UI_AccentHover = RGB(0x5C, 0x7C, 0xFF);
const COLORREF Gvtcier_UI_AccentPressed= RGB(0x35, 0x57, 0xD8);
const COLORREF Gvtcier_UI_AccentSoft  = RGB(0x2E, 0x39, 0x55);
const COLORREF Gvtcier_UI_AccentDark  = RGB(0x35, 0x57, 0xD8);
const COLORREF Gvtcier_UI_White       = RGB(0xFF, 0xFF, 0xFF);
const COLORREF Gvtcier_UI_Danger      = RGB(0xF8, 0x71, 0x71);
const COLORREF Gvtcier_UI_Frame       = RGB(0x0A, 0x0B, 0x0E);

static const COLORREF kUiSuccess      = RGB(0x4A, 0xDE, 0x80);

// 整站配色表。深色底 + 蓝紫色强调色，是引擎所有原生界面与编辑器面板的统一风格。
// 颜色值与设置界面、主菜单里的硬编码颜色保持同一套色感，改主题时两边都要动。

// 取某个颜色的实心刷子，首次用到时创建并缓存。
HBRUSH Gvtcier_UI_Brush(COLORREF color) {
    auto it = g_Gvtcier_UI_Brushes.find(color);
    if (it != g_Gvtcier_UI_Brushes.end()) return it->second;
    HBRUSH b = CreateSolidBrush(color);
    g_Gvtcier_UI_Brushes[color] = b;
    return b;
}

// 在矩形内画一段文本。先选常规体、设透明背景（否则会出现白底块）、设文字色，
// 再把 UTF-8 转宽字符交给 DrawTextW，fmt 决定左对齐/居中/垂直居中/省略号等行为。
void Gvtcier_UI_Text(HDC hdc, const RECT& rc, const char* text, UINT fmt, COLORREF color) {
    if (g_uiFont) SelectObject(hdc, g_uiFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    std::wstring ws = Gvtcier_UI_W(text);
    DrawTextW(hdc, ws.c_str(), -1, (RECT*)&rc, fmt);
}

// 矩形填充。这里没有走 Gvtcier_UI_Brush 的缓存，每次临时创建再删除，
// 因为填充调用密度低，缓存反而会积累大量只用一次的刷子。
void Gvtcier_UI_Fill(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

// 圆角实心填充。半径先夹到短边的一半（否则 GDI 会画出畸形圆角），
// 再用圆角区域裁剪并填充，右/下边界各 +1 是因为 CreateRoundRectRgn 用开区间。
void Gvtcier_UI_FillRound(HDC hdc, const RECT& rc, COLORREF color, int radius) {
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int r = radius < 0 ? 0 : (radius > (w < h ? w : h) / 2 ? (w < h ? w : h) / 2 : radius);
    HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, r * 2, r * 2);
    FillRgn(hdc, rgn, Gvtcier_UI_Brush(color));
    DeleteObject(rgn);
}

// 圆角矩形描边。GDI 的 RoundRect 会同时用画笔描边和画刷填充，
// 所以这里把画刷换成 NULL_BRUSH 保证只出线不出面。
void Gvtcier_UI_RoundRect(HDC hdc, const RECT& rc, COLORREF color, int width, int radius) {
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int r = radius < 0 ? 0 : (radius > (w < h ? w : h) / 2 ? (w < h ? w : h) / 2 : radius);
    HPEN pn = CreatePen(PS_SOLID, width, color);
    HPEN oldPn = (HPEN)SelectObject(hdc, pn);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(hdc, oldPn);
    SelectObject(hdc, oldBr);
    DeleteObject(pn);
}

// 画一条 1 像素横线，用作分隔线。用完立刻把旧画笔选回去再删新笔，
// 顺序反了会导致 DC 里还引用着一个已删除的笔。
void Gvtcier_UI_HLine(HDC hdc, int x0, int x1, int y, COLORREF color) {
    HPEN pn = CreatePen(PS_SOLID, 1, color);
    HPEN op = (HPEN)SelectObject(hdc, pn);
    MoveToEx(hdc, x0, y, nullptr);
    LineTo(hdc, x1, y);
    SelectObject(hdc, op);
    DeleteObject(pn);
}

// 画一条 1 像素竖线，用作分栏线。
void Gvtcier_UI_VLine(HDC hdc, int x, int y0, int y1, COLORREF color) {
    HPEN pn = CreatePen(PS_SOLID, 1, color);
    HPEN op = (HPEN)SelectObject(hdc, pn);
    MoveToEx(hdc, x, y0, nullptr);
    LineTo(hdc, x, y1);
    SelectObject(hdc, op);
    DeleteObject(pn);
}

// 画一行列表项。选中时底色换成 ItemSelBg，并在最左侧画一根 3 像素宽的强调色竖条，
// 文字用亮色；未选中用暗色文字。文字左侧缩进 12 像素。
void Gvtcier_UI_ItemList(HDC hdc, const RECT& rc, const char* text, bool sel) {
    Gvtcier_UI_Fill(hdc, rc, sel ? Gvtcier_UI_ItemSelBg : Gvtcier_UI_Item);
    RECT lr = rc;
    lr.left += 2;
    if (sel) {
        RECT bar = { rc.left + 1, rc.top + 4, rc.left + 4, rc.bottom - 4 };
        Gvtcier_UI_Fill(hdc, bar, Gvtcier_UI_Accent);
    }
    RECT tr = rc;
    tr.left += 12;
    Gvtcier_UI_Text(hdc, tr, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, sel ? Gvtcier_UI_TextColor : Gvtcier_UI_TextMuted);
}

// 画自绘按钮。悬停与按下用同一个深强调色底，文字居中白色。
void Gvtcier_UI_DrawButton(HDC hdc, const RECT& rc, const char* text, bool hover, bool pressed) {
    Gvtcier_UI_Fill(hdc, rc, (hover || pressed) ? Gvtcier_UI_AccentDark : Gvtcier_UI_ItemSel);
    Gvtcier_UI_Text(hdc, rc, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, Gvtcier_UI_TextColor);
}

// 画窗口右上角的三个系统按钮图标。type 0 最小化、1 最大化、3 还原，其他值当关闭。
void Gvtcier_UI_DrawWinButton(HDC hdc, const RECT& rc, int type, bool hover) {
    const char* ch = (type == 0) ? u8"—" : (type == 1) ? u8"□" : (type == 3) ? u8"❐" : u8"×";
    Gvtcier_UI_Fill(hdc, rc, hover ? RGB(0x3A, 0x3F, 0x49) : Gvtcier_UI_BgTitlebar);
    Gvtcier_UI_Text(hdc, rc, ch, DT_CENTER | DT_VCENTER | DT_SINGLELINE, hover ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x9D, 0xA0, 0xA6));
}

// 画容器底色，纯色填充，没有边框和圆角。
void Gvtcier_UI_DrawContainer(HDC hdc, const RECT& rc, COLORREF color) {
    Gvtcier_UI_Fill(hdc, rc, color);
}

// 画卡片：圆角 8 的实心底 + 1 像素边框。
void Gvtcier_UI_DrawCard(HDC hdc, const RECT& rc, COLORREF bg, COLORREF border) {
    Gvtcier_UI_FillRound(hdc, rc, bg, 8);
    Gvtcier_UI_RoundRect(hdc, rc, border, 1, 8);
}

// 标题栏图标缓存。存的是 RGBA 原始像素（每像素 4 字节），宽高为 0 表示还没设置。
static std::vector<unsigned char> g_titleIconRgba;
static int g_titleIconW = 0, g_titleIconH = 0;

// 设置标题栏左侧要画的小图标，传 RGBA 像素与尺寸，内部整块拷一份。
void Gvtcier_UI_SetTitleIcon(const unsigned char* rgba, int w, int h) {
    g_titleIconRgba.assign(rgba, rgba + (size_t)w * h * 4);
    g_titleIconW = w;
    g_titleIconH = h;
}

// 画自绘标题栏：底色 + 底部 1 像素分隔线 + 左侧 16x16 图标 + 粗体标题 + 可选副标题。
// 有图标时用 DIB 区把 RGBA 画上去：因为 GDI 的位图是 BGRA 顺序，所以填充时要把 R 与 B 对调；
// biHeight 取负值表示自上而下的行序，跟源数据顺序一致。
// 没有图标就画一个强调色圆点当占位。副标题画在标题下方 22 像素处，用暗色。
void Gvtcier_UI_DrawTitleBar(HDC hdc, const RECT& rc, const char* title, const char* subtitle) {
    RECT tr = rc;
    Gvtcier_UI_Fill(hdc, tr, Gvtcier_UI_BgTitlebar);
    Gvtcier_UI_HLine(hdc, rc.left, rc.right, rc.bottom - 1, Gvtcier_UI_BorderSubtle);
    int iconY = rc.top + (rc.bottom - rc.top - 16) / 2;
    RECT ir = { rc.left + 14, iconY, rc.left + 30, iconY + 16 };
    if (g_titleIconW > 0 && g_titleIconH > 0 && !g_titleIconRgba.empty()) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = g_titleIconW;
        bi.bmiHeader.biHeight = -g_titleIconH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP hbm = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (hbm && bits) {
            const unsigned char* src = g_titleIconRgba.data();
            unsigned char* dst = (unsigned char*)bits;
            for (int i = 0; i < g_titleIconW * g_titleIconH; ++i) {
                dst[i * 4 + 0] = src[i * 4 + 2];
                dst[i * 4 + 1] = src[i * 4 + 1];
                dst[i * 4 + 2] = src[i * 4 + 0];
                dst[i * 4 + 3] = src[i * 4 + 3];
            }
            HDC memDc = CreateCompatibleDC(hdc);
            HGDIOBJ old = SelectObject(memDc, hbm);
            StretchBlt(hdc, ir.left, ir.top, ir.right - ir.left, ir.bottom - ir.top,
                       memDc, 0, 0, g_titleIconW, g_titleIconH, SRCCOPY);
            SelectObject(memDc, old);
            DeleteDC(memDc);
            DeleteObject(hbm);
        }
    } else {
        COLORREF dot = RGB(0x4A, 0x6C, 0xF7);
        HBRUSH br = CreateSolidBrush(dot);
        HPEN pn = CreatePen(PS_SOLID, 1, dot);
        HPEN op = (HPEN)SelectObject(hdc, pn);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, br);
        Ellipse(hdc, ir.left, ir.top, ir.right, ir.bottom);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(br);
        DeleteObject(pn);
    }
    RECT tx = { rc.left + 40, iconY, rc.right - 12, iconY + 16 };
    if (g_uiFontBold) SelectObject(hdc, g_uiFontBold);
    Gvtcier_UI_Text(hdc, tx, title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, Gvtcier_UI_TextColor);
    if (g_uiFontBold) SelectObject(hdc, g_uiFont);
    if (subtitle && subtitle[0]) {
        RECT sx = { rc.left + 40, rc.top + 22, rc.right - 12, rc.top + 22 + 14 };
        Gvtcier_UI_Text(hdc, sx, subtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE, Gvtcier_UI_TextDim);
    }
}

// 画工具栏底条，与标题栏一样靠一条细分隔线和下方内容分开。
void Gvtcier_UI_DrawToolbar(HDC hdc, const RECT& rc) {
    Gvtcier_UI_Fill(hdc, rc, Gvtcier_UI_BgToolbar);
    Gvtcier_UI_HLine(hdc, rc.left, rc.right, rc.bottom - 1, Gvtcier_UI_BorderSubtle);
}

// 画面板标题带：面板底色 + 底部分隔线 + 左侧粗体标题。
void Gvtcier_UI_DrawHeaderBand(HDC hdc, const RECT& rc, const char* text) {
    RECT bg = rc;
    bg.bottom -= 1;
    Gvtcier_UI_Fill(hdc, bg, Gvtcier_UI_BgPanel);
    Gvtcier_UI_HLine(hdc, rc.left, rc.right, rc.bottom - 1, Gvtcier_UI_BorderSubtle);
    RECT tr = rc;
    tr.left += 12;
    tr.top += 0;
    if (g_uiFontBold) SelectObject(hdc, g_uiFontBold);
    Gvtcier_UI_Text(hdc, tr, text, DT_LEFT | DT_VCENTER | DT_SINGLELINE, Gvtcier_UI_TextColor);
    if (g_uiFontBold) SelectObject(hdc, g_uiFont);
}

// 画编辑器中间的视口外框：深色外圈 1 像素当“屏幕边框”，内嵌主底色，
// 顶部 28 像素做一条渐变顶栏，右侧写"场景编辑"，左侧画绿点 + "已连接"。
// 这两段中文是用十六进制 UTF-8 转义写的（避免源文件编码问题），
// 实测就是"场景编辑"和"已连接"两个词。
void Gvtcier_UI_DrawViewportFrame(HDC hdc, const RECT& rc) {
    RECT outer = rc;
    Gvtcier_UI_Fill(hdc, outer, Gvtcier_UI_Frame);
    RECT inner = { rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1 };
    Gvtcier_UI_Fill(hdc, inner, Gvtcier_UI_Bg);
    RECT top = { rc.left, rc.top, rc.right, rc.top + 28 };
    fillGradientV(hdc, top, RGB(0x22, 0x25, 0x2C), RGB(0x18, 0x1A, 0x1F));
    Gvtcier_UI_HLine(hdc, rc.left, rc.right, rc.top + 28, Gvtcier_UI_BorderSubtle);
    RECT ir = { rc.right - 110, rc.top + 6, rc.right - 8, rc.top + 22 };
    if (g_uiFontBold) SelectObject(hdc, g_uiFontBold);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, Gvtcier_UI_TextColor);
    {
        std::wstring t = Gvtcier_UI_W(u8"\xE5\x9C\xBA\xE6\x99\xAF\xE7\xBC\x96\xE8\xBE\x91");
        DrawTextW(hdc, t.c_str(), -1, &ir, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    if (g_uiFontBold) SelectObject(hdc, g_uiFont);
    RECT lr = { rc.left + 10, rc.top + 6, rc.left + 200, rc.top + 22 };
    SetTextColor(hdc, Gvtcier_UI_TextMuted);
    RECT ldot = { rc.left + 10, rc.top + 11, rc.left + 16, rc.top + 17 };
    HBRUSH lbr = CreateSolidBrush(kUiSuccess);
    HPEN lpn = CreatePen(PS_SOLID, 1, kUiSuccess);
    HPEN lop = (HPEN)SelectObject(hdc, lpn);
    HBRUSH lob = (HBRUSH)SelectObject(hdc, lbr);
    Ellipse(hdc, ldot.left, ldot.top, ldot.right, ldot.bottom);
    SelectObject(hdc, lop);
    SelectObject(hdc, lob);
    DeleteObject(lbr);
    DeleteObject(lpn);
    RECT ltx = { rc.left + 22, rc.top + 6, rc.left + 160, rc.top + 22 };
    {
        std::wstring t = Gvtcier_UI_W(u8"\xE5\xB7\xB2\xE8\xBF\x9E\xE6\x8E\xA5");
        DrawTextW(hdc, t.c_str(), -1, &ltx, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

// 画标签栏底条，样式与工具栏一致。
void Gvtcier_UI_DrawTabBar(HDC hdc, const RECT& rc) {
    Gvtcier_UI_Fill(hdc, rc, Gvtcier_UI_BgToolbar);
    Gvtcier_UI_HLine(hdc, rc.left, rc.right, rc.bottom - 1, Gvtcier_UI_BorderSubtle);
}

// 画输入框外框：深色底 + 圆角 3 的 1 像素边框。控件本身由原生 EDIT 负责。
void Gvtcier_UI_DrawInputBox(HDC hdc, const RECT& rc) {
    Gvtcier_UI_Fill(hdc, rc, Gvtcier_UI_BgInput);
    Gvtcier_UI_RoundRect(hdc, rc, Gvtcier_UI_Border, 1, 3);
}

// 画一个小圆角标签（tag），左右各留 6 像素内边距，文字居中。
void Gvtcier_UI_DrawTag(HDC hdc, const RECT& rc, const char* text, COLORREF bg, COLORREF fg) {
    Gvtcier_UI_Fill(hdc, rc, bg);
    Gvtcier_UI_RoundRect(hdc, rc, bg, 1, 8);
    RECT tr = rc;
    tr.left += 6;
    tr.right -= 6;
    Gvtcier_UI_Text(hdc, tr, text, DT_CENTER | DT_VCENTER | DT_SINGLELINE, fg);
}

// 画柔和的投影：从外往里画一圈圈逐渐加粗的圆角描边，透明度从 0 递增到 60/255，
// 叠加出模糊效果。size 是扩散半径，逐层偏移 (i,i) 形成右下方向的投影。
void Gvtcier_UI_DrawShadow(HDC hdc, const RECT& rc, int size, COLORREF color, int radius) {
    if (size <= 0) return;
    for (int i = size; i > 0; --i) {
        int a = 60 * (size - i + 1) / size;
        COLORREF c = RGB((GetRValue(color) * a) / 255, (GetGValue(color) * a) / 255, (GetBValue(color) * a) / 255);
        RECT sr = { rc.left + i, rc.top + i, rc.right + i, rc.bottom + i };
        HPEN pn = CreatePen(PS_SOLID, 1, c);
        HPEN op = (HPEN)SelectObject(hdc, pn);
        HBRUSH ob = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, sr.left, sr.top, sr.right, sr.bottom, radius, radius);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pn);
    }
}

// 画编辑器状态栏。有 error 文本时左侧显示红色错误信息（带省略号），
// 否则显示普通 left 文本；right 文本右对齐显示在暗色里。
void Gvtcier_UI_DrawEditorStatus(HDC hdc, const RECT& rc, const char* left, const char* right, const char* error) {
    Gvtcier_UI_Fill(hdc, rc, Gvtcier_UI_BgStatusbar);
    Gvtcier_UI_HLine(hdc, rc.left, rc.right, rc.top, Gvtcier_UI_BorderSubtle);
    if (error && error[0]) {
        RECT er = rc;
        er.left += 10;
        Gvtcier_UI_Text(hdc, er, error, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS, Gvtcier_UI_Danger);
    } else {
        RECT lr = rc;
        lr.left += 10;
        Gvtcier_UI_Text(hdc, lr, left, DT_LEFT | DT_VCENTER | DT_SINGLELINE, Gvtcier_UI_TextMuted);
    }
    if (right && right[0]) {
        RECT rr = rc;
        rr.right -= 10;
        Gvtcier_UI_Text(hdc, rr, right, DT_RIGHT | DT_VCENTER | DT_SINGLELINE, Gvtcier_UI_TextDim);
    }
}
