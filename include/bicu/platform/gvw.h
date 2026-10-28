#pragma once

#include "bicu/core/export.h"
#include <stdint.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __ANDROID__
#include <android/native_window.h>
#endif

struct GvWWindow {
    int width = 0;
    int height = 0;
    bool quit = false;
    bool focused = true;
#ifdef _WIN32
    HWND hwnd = nullptr;
#elif defined(__ANDROID__)
    ANativeWindow* nwin = nullptr;
#endif
    void* sys = nullptr;
};

GVTCIER_API bool GvW_Create(GvWWindow* w, const char* title, int width, int height);
GVTCIER_API bool GvW_CreateIn(GvWWindow* w, const char* title, int width, int height, void* parentHwnd);
GVTCIER_API void GvW_Destroy(GvWWindow* w);
GVTCIER_API bool GvW_Poll(GvWWindow* w);
GVTCIER_API void GvW_SwapBuffers(GvWWindow* w);

GVTCIER_API int GvW_Width(const GvWWindow* w);
GVTCIER_API int GvW_Height(const GvWWindow* w);
GVTCIER_API bool GvW_ShouldQuit(const GvWWindow* w);

GVTCIER_API bool GvW_IsKeyDown(const GvWWindow* w, int key);
GVTCIER_API int GvW_ConsumeKeyPress(GvWWindow* w);
GVTCIER_API int GvW_ConsumeScroll(GvWWindow* w);
GVTCIER_API void GvW_MousePos(const GvWWindow* w, int* x, int* y);
GVTCIER_API void GvW_MouseDelta(const GvWWindow* w, int* dx, int* dy);
GVTCIER_API bool GvW_MouseDown(const GvWWindow* w, int button);
GVTCIER_API bool GvW_GrabMouse(GvWWindow* w, bool grab);
GVTCIER_API void GvW_ReleaseAllKeys(GvWWindow* w);
GVTCIER_API void GvW_SetInputEnabled(GvWWindow* w, bool enabled);
GVTCIER_API void GvW_SetAutoGrab(GvWWindow* w, bool on);
GVTCIER_API bool GvW_IsForeground(const GvWWindow* w);
GVTCIER_API void GvW_UpdateClipCursorHide(GvWWindow* w);
GVTCIER_API void GvW_RestoreCursor(GvWWindow* w);
GVTCIER_API void GvW_ApplyMode(GvWWindow* w, int mode);
GVTCIER_API int GvW_Mode(const GvWWindow* w);
typedef void (*GvWResizeFn)(int w, int h);
GVTCIER_API void GvW_SetResizeCallback(GvWResizeFn fn);

GVTCIER_API bool GvW_GL_Init(GvWWindow* w);
GVTCIER_API void GvW_GL_Shutdown(GvWWindow* w);
GVTCIER_API void* GvW_GL_Proc(const char* name);
GVTCIER_API void GvW_GL_SetVsync(bool vsync);

#ifdef __ANDROID__
GVTCIER_API void GvW_Android_SetNativeWindow(void* win);
GVTCIER_API void GvW_Android_ClearNativeWindow();
GVTCIER_API void GvW_Android_SetTouch(int pointerId, int x, int y, int action, int actionIndex);
GVTCIER_API void GvW_Android_ClearTouches();
#endif
