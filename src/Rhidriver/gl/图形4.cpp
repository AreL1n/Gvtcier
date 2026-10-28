#include "Gvtcier.h"
#include <cstdio>

// 桌面 GL 的上下文创建与函数装载。
// 流程分两步：
//   1. setupContext 先建一个临时窗口与临时上下文，用 wglGetProcAddress 取到
//      WGL_ARB_pixel_format / WGL_ARB_create_context 扩展，再用它给真正的窗口建 4.6 核心上下文；
//   2. glInit 依次把 GL 函数地址取回到全局 gl 结构体。
// 之所以要临时窗口，是因为像素格式一旦设定就不能更改，而创建核心上下文必须先有扩展函数，
// 扩展函数又只能通过已激活的上下文获取。

GL gl;

static HDC g_hdc = nullptr;    // 窗口设备上下文
static HWND g_hwnd = nullptr;  // 绑定的窗口句柄
static HGLRC g_ctx = nullptr;  // 主渲染上下文（4.6 核心 profile）
static bool g_vsync = true;    // 垂直同步期望状态，创建上下文后立即应用
static int g_width = 0, g_height = 0;  // 最近一次记录的客户区尺寸

// wglSwapIntervalEXT 的函数指针类型，用于控制交换间隔（垂直同步）。
typedef BOOL (APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int);

// 为 hwnd 建立像素格式与 OpenGL 4.6 核心上下文。成功返回 true。
// 失败路径会完整清理临时窗口、临时上下文与 DC，避免残留句柄。
static bool setupContext(HWND hwnd) {
    // 上下文属性：0x2091 = WGL_CONTEXT_MAJOR_VERSION_ARB -> 4，
    // 0x2092 = WGL_CONTEXT_MINOR_VERSION_ARB -> 6，
    // 0x9126 = WGL_CONTEXT_PROFILE_MASK_ARB，0x00000001 = WGL_CONTEXT_CORE_PROFILE_BIT_ARB。
    const int kCtxAttribs[] = { 0x2091, 4, 0x2092, 6, 0x9126, 0x00000001, 0 };
    // 传统像素格式描述，用于临时窗口以及扩展不可用时的回退路径。
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SWAP_COPY;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    // WGL 扩展函数指针类型。
    typedef BOOL  (APIENTRY *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
    typedef HGLRC (APIENTRY *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
    typedef BOOL  (APIENTRY *PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC, int, int, UINT, const int*, int*);
    PFNWGLCHOOSEPIXELFORMATARBPROC choosePF = nullptr;
    PFNWGLCREATECONTEXTATTRIBSARBPROC createCtx = nullptr;
    PFNWGLGETPIXELFORMATATTRIBIVARBPROC getPfAttr = nullptr;

    g_hwnd = hwnd;
    g_hdc = GetDC(hwnd);
    HGLRC dummy = nullptr;
    // 统一失败清理：释放临时上下文、主上下文与 DC。
    auto fail = [&]() -> bool {
        if (dummy) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(dummy); dummy = nullptr; }
        if (g_ctx) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(g_ctx); g_ctx = nullptr; }
        if (g_hdc) { ReleaseDC(g_hwnd, g_hdc); g_hdc = nullptr; }
        return false;
    };

    HWND tempWnd = nullptr;
    HDC tempDc = nullptr;
    // 清理临时窗口相关资源：上下文、DC、窗口与窗口类注册。
    auto killTemp = [&]() {
        if (dummy) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(dummy); dummy = nullptr; }
        if (tempDc) { ReleaseDC(tempWnd, tempDc); tempDc = nullptr; }
        if (tempWnd) { DestroyWindow(tempWnd); tempWnd = nullptr; }
        UnregisterClassW(L"GvtcierGLTemp", GetModuleHandleW(nullptr));
    };

    // 注册并创建一个 8x8 的隐藏临时窗口，仅用于拿到 WGL 扩展函数地址。
    WNDCLASSW twc = {};
    twc.style = CS_OWNDC;
    twc.lpfnWndProc = DefWindowProcW;
    twc.hInstance = GetModuleHandleW(nullptr);
    twc.lpszClassName = L"GvtcierGLTemp";
    if (!RegisterClassW(&twc)) { printf("[gl] temp window class failed\n"); return fail(); }
    tempWnd = CreateWindowW(L"GvtcierGLTemp", L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!tempWnd) { printf("[gl] temp window failed\n"); return fail(); }
    tempDc = GetDC(tempWnd);
    int pfTemp = ChoosePixelFormat(tempDc, &pfd);
    if (!pfTemp || !SetPixelFormat(tempDc, pfTemp, &pfd)) { printf("[gl] temp pixel format failed\n"); killTemp(); return fail(); }
    dummy = wglCreateContext(tempDc);
    if (!dummy || !wglMakeCurrent(tempDc, dummy)) { printf("[gl] dummy WGL context failed\n"); killTemp(); return fail(); }

    // 在临时上下文有效期间取三个 WGL_ARB 扩展入口，之后临时资源即可全部销毁。
    choosePF = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
    createCtx = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    getPfAttr = (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)wglGetProcAddress("wglGetPixelFormatAttribivARB");
    killTemp();
    if (!choosePF || !createCtx) {
        printf("[gl] WGL_ARB extensions unavailable\n");
        return fail();
    }

    // 用 ARB 接口挑选像素格式：
    // 0x2001 = WGL_DRAW_TO_WINDOW_ARB 为 1，0x2010 = WGL_SUPPORT_OPENGL_ARB 为 1，
    // 0x2011 = WGL_DOUBLE_BUFFER_ARB 为 1，
    // 0x2003 = WGL_ACCELERATION_ARB 要求 0x2027 = WGL_FULL_ACCELERATION_ARB（硬件加速），
    // 0x2013 = WGL_SWAP_METHOD_ARB 要求 0x202B = WGL_SWAP_COPY_ARB，
    // 0x2014 = WGL_COLOR_BITS_ARB 32，0x2022 = WGL_DEPTH_BITS_ARB 24，0x2023 = WGL_STENCIL_BITS_ARB 8。
    const int pixAttrs[] = {
        0x2001, 1,
        0x2010, 1,
        0x2011, 1,
        0x2003, 0x2027,
        0x2013, 0x202B,
        0x2014, 32,
        0x2022, 24,
        0x2023, 8,
        0
    };
    int pfs[32] = {};
    UINT numFormats = 0;
    int pf = 0;
    if (choosePF(g_hdc, pixAttrs, nullptr, 32, pfs, &numFormats) && numFormats > 0) {
        pf = pfs[0];
        if (getPfAttr) {
            // 0x2007 = WGL_SWAP_METHOD_ARB，0x2029 = WGL_SWAP_UNDEFINED_ARB；
            // 优先挑交换方式未定义（由驱动决定，通常最优）的格式。
            const int swapAttr[1] = { 0x2007 };
            for (UINT i = 0; i < numFormats; ++i) {
                int v[1] = { 0 };
                if (getPfAttr(g_hdc, pfs[i], 0, 1, swapAttr, v) && v[0] == 0x2029) { pf = pfs[i]; break; }
            }
        }
        if (!SetPixelFormat(g_hdc, pf, &pfd)) { printf("[gl] SetPixelFormat failed\n"); return fail(); }
    } else {
        // ARB 接口没给出可用格式时退回传统 ChoosePixelFormat，兼容老旧驱动。
        printf("[gl] wglChoosePixelFormatARB empty, legacy pixel format\n");
        pf = ChoosePixelFormat(g_hdc, &pfd);
        if (!pf || !SetPixelFormat(g_hdc, pf, &pfd)) { printf("[gl] legacy SetPixelFormat failed\n"); return fail(); }
    }

    g_ctx = createCtx(g_hdc, nullptr, kCtxAttribs);
    if (!g_ctx) {
        printf("[gl] OpenGL 4.6 core context creation failed\n");
        return fail();
    }
    wglMakeCurrent(g_hdc, g_ctx);
    return true;
}

// 初始化 GL 后端：建上下文 → 取函数地址 → 读尺寸并应用垂直同步。
// 任一必需函数缺失都会打印日志并把 ok 置 false，最终返回失败。
bool glInit(HWND hwnd) {
    if (!setupContext(hwnd)) return false;

    HMODULE dll = GetModuleHandleA("opengl32.dll");
    if (!dll) dll = LoadLibraryA("opengl32.dll");
    if (!dll) {
        printf("[gl] cannot load opengl32.dll\n");
        return false;
    }

    // LOAD_11 用于 GL 1.1 函数（由 opengl32.dll 直接导出），
    // LOAD_GL 用于 1.2 及以上函数（必须经 wglGetProcAddress 取）。
    bool ok = true;
#define LOAD_11(name) do { gl.name = (name##Fn)GetProcAddress(dll, #name); if (!gl.name) { printf("[gl] missing GL 1.1 function: %s\n", #name); ok = false; } } while (0)
#define LOAD_GL(name)  do { gl.name = (name##Fn)wglGetProcAddress(#name); if (!gl.name) { printf("[gl] missing GL function: %s\n", #name); ok = false; } } while (0)

    LOAD_11(glClear); LOAD_11(glClearColor); LOAD_11(glClearDepth); LOAD_11(glViewport);
    LOAD_11(glEnable); LOAD_11(glDisable); LOAD_11(glDepthFunc); LOAD_11(glDepthMask);
    LOAD_11(glStencilFunc); LOAD_11(glStencilOp); LOAD_11(glStencilMask); LOAD_11(glClearStencil); LOAD_11(glColorMask);
    LOAD_11(glBlendFunc);
    LOAD_11(glCullFace); LOAD_11(glFrontFace); LOAD_11(glScissor); LOAD_11(glPolygonMode); LOAD_11(glPolygonOffset);
    LOAD_11(glDrawBuffer); LOAD_GL(glDrawBuffers); LOAD_11(glReadBuffer); LOAD_11(glReadPixels);
    LOAD_GL(glCopyTexSubImage2D);
    LOAD_GL(glMapBuffer); LOAD_GL(glUnmapBuffer);
    LOAD_11(glGenTextures); LOAD_11(glBindTexture); LOAD_11(glDeleteTextures);
    LOAD_11(glTexImage2D); LOAD_11(glTexParameteri); LOAD_11(glTexParameterf);
    LOAD_GL(glTexSubImage2D); LOAD_GL(glGenerateMipmap);
    LOAD_GL(glTexImage3D); LOAD_GL(glTexSubImage3D);
    LOAD_11(glGetString); LOAD_11(glGetIntegerv); LOAD_11(glGetError);

    LOAD_GL(glActiveTexture);
    LOAD_GL(glGenVertexArrays); LOAD_GL(glBindVertexArray); LOAD_GL(glDeleteVertexArrays);
    LOAD_GL(glGenBuffers); LOAD_GL(glBindBuffer); LOAD_GL(glBufferData); LOAD_GL(glDeleteBuffers);
    LOAD_GL(glBufferSubData); LOAD_GL(glBindBufferBase);
    LOAD_GL(glGetUniformBlockIndex); LOAD_GL(glUniformBlockBinding);
    LOAD_GL(glGetActiveUniformBlockiv); LOAD_GL(glGetUniformIndices);
    LOAD_GL(glGetActiveUniformsiv); LOAD_GL(glGetActiveUniformName);
    LOAD_GL(glDispatchCompute); LOAD_GL(glMemoryBarrier);
    LOAD_GL(glVertexAttribPointer); LOAD_GL(glEnableVertexAttribArray);
    LOAD_GL(glDrawElementsInstanced); LOAD_GL(glVertexAttribDivisor);
    LOAD_GL(glCreateShader); LOAD_GL(glShaderSource); LOAD_GL(glCompileShader);
    LOAD_GL(glGetShaderiv); LOAD_GL(glGetShaderInfoLog); LOAD_GL(glDeleteShader);
    LOAD_GL(glCreateProgram); LOAD_GL(glAttachShader); LOAD_GL(glLinkProgram);
    LOAD_GL(glGetProgramiv); LOAD_GL(glGetProgramInfoLog); LOAD_GL(glDeleteProgram);
    LOAD_GL(glUseProgram); LOAD_GL(glGetUniformLocation);
    LOAD_GL(glUniform1i); LOAD_GL(glUniform1f); LOAD_GL(glUniform2f); LOAD_GL(glUniform3f); LOAD_GL(glUniform4f); LOAD_GL(glUniformMatrix4fv);
    LOAD_GL(glDrawArrays); LOAD_GL(glDrawElements);
    LOAD_GL(glGenFramebuffers); LOAD_GL(glBindFramebuffer); LOAD_GL(glDeleteFramebuffers);
    LOAD_GL(glFramebufferTexture2D); LOAD_GL(glCheckFramebufferStatus);
    LOAD_GL(glFramebufferTextureLayer);
    LOAD_GL(glBlitFramebuffer);
    LOAD_GL(glGenRenderbuffers); LOAD_GL(glBindRenderbuffer); LOAD_GL(glDeleteRenderbuffers);
    LOAD_GL(glRenderbufferStorage); LOAD_GL(glFramebufferRenderbuffer);

#undef LOAD_11
#undef LOAD_GL
    if (ok) {
        // 全部函数就绪后记录客户区尺寸，并按 g_vsync 立即设置交换间隔。
        RECT rc;
        GetClientRect(g_hwnd, &rc);
        g_width = rc.right - rc.left;
        g_height = rc.bottom - rc.top;
        PFNWGLSWAPINTERVALEXTPROC swapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        if (swapInterval) swapInterval(g_vsync ? 1 : 0);
        printf("[gl] opengl 4.6 core ready %dx%d\n", g_width, g_height);
    }
    return ok;
}

// 交换前后缓冲，把本帧画面显示到窗口。
void glPresent() {
    SwapBuffers(g_hdc);
}

// 设置垂直同步：记录状态并立即调用 wglSwapIntervalEXT（1 开启，0 关闭）。
void glSetVsync(bool vsync) {
    g_vsync = vsync;
    PFNWGLSWAPINTERVALEXTPROC swapInterval = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (swapInterval) swapInterval(vsync ? 1 : 0);
}

// 记录新的绘制尺寸。真正的视口由 Gvtcier_Rhi_Viewport 设置，这里只是保存数值。
void glResize(int w, int h) {
    g_width = w;
    g_height = h;
}

// 兼容（固定管线）上下文句柄，与主核心上下文共享对象（纹理/缓冲）。
static HGLRC g_legacyCtx = nullptr;

// 创建 2.1 兼容上下文，供 glBegin/glEnd 等固定管线接口使用。
// 优先用 wglCreateContextAttribsARB 以共享主上下文的方式创建；
// 失败时退回老式 wglCreateContext 并用 wglShareLists 手动共享。
// 已存在则直接返回 true。
bool glLegacyCreate() {
    if (g_legacyCtx) return true;
    if (!g_hdc || !g_ctx) {
        printf("[gl1] 主上下文未就绪，无法创建兼容上下文\n");
        return false;
    }
    typedef HGLRC (APIENTRY *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
    PFNWGLCREATECONTEXTATTRIBSARBPROC createCtx =
        (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createCtx) {
        printf("[gl1] wglCreateContextAttribsARB 不可用\n");
        return false;
    }
    // 0x2091 = WGL_CONTEXT_MAJOR_VERSION_ARB -> 2，
    // 0x2092 = WGL_CONTEXT_MINOR_VERSION_ARB -> 1；不指定 profile 即为兼容 profile。
    const int legacyAttribs[] = {
        0x2091, 2,
        0x2092, 1,
        0
    };
    HGLRC ctx = createCtx(g_hdc, g_ctx, legacyAttribs);
    if (!ctx) {
        printf("[gl1] 兼容 profile 2.1 上下文创建失败，回退旧式上下文\n");
        ctx = wglCreateContext(g_hdc);
        if (ctx && !wglShareLists(g_ctx, ctx)) {
            printf("[gl1] 上下文共享失败，仍继续\n");
        }
    }
    if (!ctx) {
        printf("[gl1] 兼容上下文创建失败\n");
        return false;
    }
    g_legacyCtx = ctx;
    printf("[gl1] 兼容上下文已创建\n");
    return true;
}

// 把兼容上下文设为当前上下文，固定管线绘制前调用。
bool glLegacyBind() {
    if (!g_legacyCtx) return false;
    return wglMakeCurrent(g_hdc, g_legacyCtx) != FALSE;
}

// 把核心上下文重新设为当前上下文，固定管线绘制结束后必须调用。
bool glCoreBind() {
    if (!g_ctx) return false;
    return wglMakeCurrent(g_hdc, g_ctx) != FALSE;
}

// 销毁兼容上下文。注意：该函数在 图形4.cpp 内部使用，
// RHI 的公开接口只暴露创建/切换，没有对应销毁入口。
bool glLegacyDestroy() {
    if (!g_legacyCtx) return false;
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_legacyCtx);
    g_legacyCtx = nullptr;
    return true;
}

// 关闭 GL 后端：先释放兼容与核心上下文，再释放 DC。
void glShutdown() {
    if (g_legacyCtx) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_legacyCtx);
        g_legacyCtx = nullptr;
    }
    if (g_ctx) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(g_ctx);
        g_ctx = nullptr;
    }
    if (g_hdc && g_hwnd) {
        ReleaseDC(g_hwnd, g_hdc);
        g_hdc = nullptr;
    }
    g_hwnd = nullptr;
}
