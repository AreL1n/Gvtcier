#pragma once
#include "bicu/core/export.h"
#include "bicu/core/conventions.h"
#include "bicu/platform/gvw.h"
#include <windows.h>
#include <commctrl.h>
#include <string>

// 引擎窗口封装。真正的 Win32 逻辑都在 GvW 模块，这里只做一层薄包装，
// 把窗口句柄、输入状态与渲染后端的创建/销毁串起来。
class Window {
public:
    // 创建顶层窗口；parent 非空时创建子窗口（用于嵌入编辑器面板）。
    GVTCIER_API bool Gvtcier_Window_Create(const char* title, int w, int h, HWND parent = nullptr);
    // 先关渲染后端再销毁窗口，顺序不能反。
    GVTCIER_API void Gvtcier_Window_Destroy();
    // 处理一轮消息，返回 false 表示该退出。
    GVTCIER_API bool Gvtcier_Window_Poll();
    // 交换前后缓冲。
    GVTCIER_API void Gvtcier_Window_SwapBuffers();

    // 查询某虚拟键是否按下。
    bool isKeyDown(int vk) const { return GvW_IsKeyDown(&gw_, vk); }
    // 取走一个本帧新按下的键，没有返回 0。
    GVTCIER_API int Gvtcier_Window_ConsumeKeyPress();
    // 窗口是否有焦点。
    bool focused() const { return gw_.focused; }
    // 取走本帧滚轮增量，读取后清零。
    GVTCIER_API int Gvtcier_Window_ConsumeScroll();

    // 捕获/释放鼠标。
    GVTCIER_API bool Gvtcier_Window_GrabMouse(bool grab);
    // 取走并清零本帧鼠标相对位移。
    GVTCIER_API void Gvtcier_Window_MouseDelta(int& dx, int& dy);
    // 读取鼠标客户区坐标。
    GVTCIER_API void Gvtcier_Window_CursorPos(int& x, int& y) const;
    // 是否为前台窗口。
    GVTCIER_API bool Gvtcier_Window_IsForeground() const;
    // 开关键盘输入。
    GVTCIER_API void Gvtcier_Window_SetInputEnabled(bool enabled);
    // 开关自动捕获鼠标。
    GVTCIER_API void Gvtcier_Window_SetAutoGrab(bool on);
    // 清空所有按键按下状态，失焦时必调。
    GVTCIER_API void Gvtcier_Window_ReleaseAllKeys();
    // 应用窗口模式：0 全屏 / 1 无边框 / 2 最大化 / 3 窗口。
    GVTCIER_API void Gvtcier_Window_ApplyMode(int mode);
    // 刷新鼠标裁剪区与光标隐藏状态。
    GVTCIER_API void Gvtcier_Window_UpdateClipCursorHide();
    // 恢复系统光标显示。
    GVTCIER_API void Gvtcier_Window_RestoreCursor();

    int width() const { return GvW_Width(&gw_); }
    int height() const { return GvW_Height(&gw_); }
    int mode() const { return GvW_Mode(&gw_); }
    HWND hwnd() const { return gw_.hwnd; }

private:
    GvWWindow gw_ = {};
};

// 原生控件字体管理：从资源段加载字体注册到进程，并提供常规体/粗体句柄。
GVTCIER_API bool Gvtcier_UI_LoadFont(const char* regularRes, const char* boldRes, const wchar_t* familyName, int px);
GVTCIER_API HFONT Gvtcier_UI_Font();
GVTCIER_API void Gvtcier_UI_ReleaseFont();
// 释放缓存的 GDI 刷子。
GVTCIER_API void Gvtcier_UI_ReleaseBrushes();
// 设置标题栏左侧图标（RGBA 像素 + 尺寸）。
GVTCIER_API void Gvtcier_UI_SetTitleIcon(const unsigned char* rgba, int w, int h);
// 给窗口及全部子控件套字体。
GVTCIER_API void Gvtcier_UI_ApplyFont(HWND hwnd, HFONT font);
GVTCIER_API void Gvtcier_UI_SelectFont(HDC hdc);
GVTCIER_API void Gvtcier_UI_SelectFontBold(HDC hdc);

// 检查当前系统代码页是否 UTF-8。
GVTCIER_API bool Gvtcier_UI_EnsureUtf8();

// UTF-8 与宽字符互转、以及读控件 UTF-8 文本。
GVTCIER_API std::wstring Gvtcier_UI_W(const char* utf8);
GVTCIER_API void Gvtcier_UI_GetText(HWND hwnd, char* buf, int len);

// 原生控件创建（文字按 UTF-8 传入，内部转宽字符）。
GVTCIER_API HWND Gvtcier_UI_Button(HWND parent, const char* text, DWORD style, int x, int y, int w, int h, int id);
GVTCIER_API HWND Gvtcier_UI_Label(HWND parent, const char* text, int x, int y, int w, int h, int id = 0);
GVTCIER_API HWND Gvtcier_UI_Edit(HWND parent, const char* text, DWORD style, int x, int y, int w, int h, int id);
GVTCIER_API HWND Gvtcier_UI_Listbox(HWND parent, DWORD style, int x, int y, int w, int h, int id);
// 往 HMENU 追加一项。
GVTCIER_API void Gvtcier_UI_MenuAppend(HMENU m, UINT flags, UINT_PTR id, const char* text);

// 统一配色表：深色底 + 蓝紫强调色。注释按用途分组。
// 背景层：主背景、面板、卡片、工具栏、状态栏、标题栏、输入框
GVTCIER_API extern const COLORREF Gvtcier_UI_Bg;
GVTCIER_API extern const COLORREF Gvtcier_UI_BgPanel;
GVTCIER_API extern const COLORREF Gvtcier_UI_BgCard;
GVTCIER_API extern const COLORREF Gvtcier_UI_BgToolbar;
GVTCIER_API extern const COLORREF Gvtcier_UI_BgStatusbar;
GVTCIER_API extern const COLORREF Gvtcier_UI_BgTitlebar;
GVTCIER_API extern const COLORREF Gvtcier_UI_BgInput;
// 列表项三态底色
GVTCIER_API extern const COLORREF Gvtcier_UI_Item;
GVTCIER_API extern const COLORREF Gvtcier_UI_ItemHover;
GVTCIER_API extern const COLORREF Gvtcier_UI_ItemSel;
GVTCIER_API extern const COLORREF Gvtcier_UI_ItemSelBg;
// 边框四档
GVTCIER_API extern const COLORREF Gvtcier_UI_Border;
GVTCIER_API extern const COLORREF Gvtcier_UI_BorderSubtle;
GVTCIER_API extern const COLORREF Gvtcier_UI_BorderStrong;
GVTCIER_API extern const COLORREF Gvtcier_UI_BorderSel;
// 文字三档（正文、次要、极弱）
GVTCIER_API extern const COLORREF Gvtcier_UI_TextColor;
GVTCIER_API extern const COLORREF Gvtcier_UI_TextMuted;
GVTCIER_API extern const COLORREF Gvtcier_UI_TextDim;
// 强调色及其变体
GVTCIER_API extern const COLORREF Gvtcier_UI_Accent;
GVTCIER_API extern const COLORREF Gvtcier_UI_AccentHover;
GVTCIER_API extern const COLORREF Gvtcier_UI_AccentPressed;
GVTCIER_API extern const COLORREF Gvtcier_UI_AccentSoft;
GVTCIER_API extern const COLORREF Gvtcier_UI_AccentDark;
GVTCIER_API extern const COLORREF Gvtcier_UI_White;
GVTCIER_API extern const COLORREF Gvtcier_UI_Danger;
GVTCIER_API extern const COLORREF Gvtcier_UI_Frame;

// GDI 绘图工具：刷子缓存、文字、填充、圆角、线条、自绘控件、各种面板装饰件。
GVTCIER_API HBRUSH Gvtcier_UI_Brush(COLORREF color);
GVTCIER_API void Gvtcier_UI_Text(HDC hdc, const RECT& rc, const char* text, UINT fmt, COLORREF color);
GVTCIER_API void Gvtcier_UI_Fill(HDC hdc, const RECT& rc, COLORREF color);
GVTCIER_API void Gvtcier_UI_FillRound(HDC hdc, const RECT& rc, COLORREF color, int radius);
GVTCIER_API void Gvtcier_UI_RoundRect(HDC hdc, const RECT& rc, COLORREF color, int width, int radius);
GVTCIER_API void Gvtcier_UI_HLine(HDC hdc, int x0, int x1, int y, COLORREF color);
GVTCIER_API void Gvtcier_UI_VLine(HDC hdc, int x, int y0, int y1, COLORREF color);
GVTCIER_API void Gvtcier_UI_DrawButton(HDC hdc, const RECT& rc, const char* text, bool hover, bool pressed);
GVTCIER_API void Gvtcier_UI_DrawWinButton(HDC hdc, const RECT& rc, int type, bool hover);
GVTCIER_API void Gvtcier_UI_ItemList(HDC hdc, const RECT& rc, const char* text, bool sel);

// 界面装饰件：容器、卡片、标题栏、工具栏、标题带、视口外框、标签栏、输入框、标签、投影。
GVTCIER_API void Gvtcier_UI_DrawContainer(HDC hdc, const RECT& rc, COLORREF color);
GVTCIER_API void Gvtcier_UI_DrawCard(HDC hdc, const RECT& rc, COLORREF bg, COLORREF border);
GVTCIER_API void Gvtcier_UI_DrawTitleBar(HDC hdc, const RECT& rc, const char* title, const char* subtitle);
GVTCIER_API void Gvtcier_UI_DrawToolbar(HDC hdc, const RECT& rc);
GVTCIER_API void Gvtcier_UI_DrawHeaderBand(HDC hdc, const RECT& rc, const char* text);
GVTCIER_API void Gvtcier_UI_DrawViewportFrame(HDC hdc, const RECT& rc);
GVTCIER_API void Gvtcier_UI_DrawTabBar(HDC hdc, const RECT& rc);
GVTCIER_API void Gvtcier_UI_DrawInputBox(HDC hdc, const RECT& rc);
GVTCIER_API void Gvtcier_UI_DrawTag(HDC hdc, const RECT& rc, const char* text, COLORREF bg, COLORREF fg);
GVTCIER_API void Gvtcier_UI_DrawShadow(HDC hdc, const RECT& rc, int size, COLORREF color, int radius);

// 画编辑器底部状态栏，error 非空时优先显示错误。
GVTCIER_API void Gvtcier_UI_DrawEditorStatus(HDC hdc, const RECT& rc, const char* left, const char* right, const char* error);
