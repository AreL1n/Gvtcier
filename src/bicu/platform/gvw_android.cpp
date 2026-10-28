#include "Gvtcier.h"
#include <cstdio>
#include <cstring>

#ifdef __ANDROID__

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <android/log.h>
#include <android/input.h>
#include <cmath>

//安卓平台的窗口层：用 EGL 创建 OpenGL ES 上下文，用纯软件方式实现虚拟按键与触屏输入。
//与 Win32 版不同，这里没有真实窗口句柄，画面直接输出到 Activity 提供的 ANativeWindow。

//EGL 上下文属性常量。老版本 EGL 头文件没定义这两个宏，这里补上，值来自 EGL 1.5 规范。
#ifndef EGL_CONTEXT_MAJOR_VERSION
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif

#define GVW_LOG(...) __android_log_print(ANDROID_LOG_INFO, "GvW", __VA_ARGS__)

//单指触摸的状态。需要保留按下位置信息，因为安卓一次 MOVE 只报当前位置，
//而虚拟摇杆必须知道这个手指最初按在哪个区域，才能判断它属于方向键还是屏幕拖拽。
struct GvWTouch {
    int id = -1;
    int x = 0;
    int y = 0;
    int xPrev = 0;
    int yPrev = 0;
    //该手指是否落在虚拟按键区域内。落在区域内时不做视角拖拽。
    bool onPad = false;
    bool active = false;
};

//安卓平台的全部状态。与 Win32 版同样以不透明指针挂在 GvWWindow::sys 上。
struct GvWAndroid {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
    //最近一次触摸位置与动作类型（0 按下 / 1 移动 / 2 抬起），供菜单统一当作"鼠标"消费。
    int32_t touchX = 0;
    int32_t touchY = 0;
    int32_t touchAction = -1;
    bool touchValid = false;
    bool touchDown = false;
    //非虚拟按键区域的手指滑动累积量，作为视角拖拽位移。
    int dragDX = 0;
    int dragDY = 0;
    //虚拟按键被触摸时合成的按键状态。
    bool keyW = false;
    bool keyA = false;
    bool keyS = false;
    bool keyD = false;
    bool keyJump = false;
    //最近一次已知的画面尺寸，用于计算虚拟按键的布局。默认按 720p 估算，等到真实尺寸再更新。
    int touchWidthCache = 1280;
    int touchHeightCache = 720;
    bool vsync = true;
    //原生窗口是否发生了变化（新建、销毁或旋转），主循环据此重新取尺寸与重建 surface。
    bool nativeWindowChanged = false;
    //当前持有的原生窗口，持有一次引用计数。
    ANativeWindow* nwin = nullptr;
    //最多同时跟踪 8 个触点，够用且避免动态分配。
    GvWTouch touches[8];
    int pointerCount = 0;
};

static GvWAndroid g_android;

namespace {

//虚拟按键到屏幕边缘的留白（像素）。
const int kPadMargin = 24;

//屏幕上的一个矩形区域，用于虚拟按键的命中检测。
struct PadRect {
    int x;
    int y;
    int w;
    int h;
};

//虚拟按键的边长：取屏幕高度的 1/5，并限制在 90~240 像素。
//按屏幕高度比例计算可适配不同分辨率与长宽比；上下限保证在小屏上不至于小到按不准，在大屏上不至于大到挡视野。
int padSize(const GvWWindow* w) {
    int s = w ? w->height / 5 : 120;
    if (s < 90) s = 90;
    if (s > 240) s = 240;
    return s;
}

//计算 WASD 虚拟方向键中某格的位置。col/row 为网格坐标：W 在 (1,0)，A/S/D 在第二行 (0..2,1)。
//行坐标做了 (1 - row) 的反转，让 row 0 位于上排；键间距取边长的 1/6，保证视觉上有间隔又不浪费空间。
PadRect wasdRect(const GvWWindow* w, int col, int row) {
    int s = padSize(w);
    int gap = s / 6;
    PadRect r;
    r.w = s;
    r.h = s;
    r.x = kPadMargin + col * (s + gap);
    r.y = w->height - kPadMargin - s - (1 - row) * (s + gap);
    return r;
}

//跳跃键的矩形：放在右下角，边长是普通按键的 1.5 倍，因为跳跃是最高频操作且常需要盲按。
PadRect jumpRect(const GvWWindow* w) {
    int s = padSize(w) * 3 / 2;
    PadRect r;
    r.w = s;
    r.h = s;
    r.x = w->width - kPadMargin - s;
    r.y = w->height - kPadMargin - s;
    return r;
}

//点是否落在矩形内。右边界与下边界取开区间，避免相邻按键在边界上重复命中。
bool inRect(const PadRect& r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

//按触点 id 查找处于活动状态的触点，找不到返回空指针。
GvWTouch* findTouch(int id) {
    for (int i = 0; i < 8; ++i)
        if (g_android.touches[i].active && g_android.touches[i].id == id)
            return &g_android.touches[i];
    return nullptr;
}

//分配一个空闲触点槽位，全部占满时返回空指针（新的手指被忽略）。
GvWTouch* allocTouch() {
    for (int i = 0; i < 8; ++i)
        if (!g_android.touches[i].active)
            return &g_android.touches[i];
    return nullptr;
}

//根据当前所有落在虚拟按键区域的手指位置，重新计算合成按键状态。
//每次都从头重算而不是增量更新，是因为多指同时按住/滑出时增量更新容易漏算，
//全量重算虽然多几次矩形判断，但逻辑简单且不会出现"按了却不响应"的卡键问题。
//这里临时构造一个只带尺寸的 GvWWindow，是为了复用按键布局函数而不依赖真实窗口对象。
void refreshPads() {
    g_android.keyW = g_android.keyA = g_android.keyS = g_android.keyD = g_android.keyJump = false;
    GvWWindow tmp;
    tmp.width = g_android.touchWidthCache;
    tmp.height = g_android.touchHeightCache;
    for (int i = 0; i < 8; ++i) {
        GvWTouch& t = g_android.touches[i];
        if (!t.active || !t.onPad) continue;
        if (inRect(wasdRect(&tmp, 1, 0), t.x, t.y)) g_android.keyW = true;
        if (inRect(wasdRect(&tmp, 0, 1), t.x, t.y)) g_android.keyA = true;
        if (inRect(wasdRect(&tmp, 1, 1), t.x, t.y)) g_android.keyS = true;
        if (inRect(wasdRect(&tmp, 2, 1), t.x, t.y)) g_android.keyD = true;
        if (inRect(jumpRect(&tmp), t.x, t.y)) g_android.keyJump = true;
    }
}

//判断某个坐标是否落在任意虚拟按键区域内。用于新手指按下时决定它是"操作按键"还是"拖拽视角"。
bool hitAnyPad(int x, int y) {
    GvWWindow tmp;
    tmp.width = g_android.touchWidthCache;
    tmp.height = g_android.touchHeightCache;
    if (inRect(wasdRect(&tmp, 1, 0), x, y)) return true;
    if (inRect(wasdRect(&tmp, 0, 1), x, y)) return true;
    if (inRect(wasdRect(&tmp, 1, 1), x, y)) return true;
    if (inRect(wasdRect(&tmp, 2, 1), x, y)) return true;
    if (inRect(jumpRect(&tmp), x, y)) return true;
    return false;
}

}

//接收来自 Java 层的触摸事件，维护多指状态并合成虚拟按键。
//pointerId 为手指编号，x/y 为屏幕坐标，action/actionIndex 与 Android MotionEvent 一致（actionIndex 当前未使用）。
//松开（UP）与取消（CANCEL）都会清空全部触点，因为 UP 表示最后一根手指离开，此时其余触点必然已失效。
void GvW_Android_SetTouch(int pointerId, int x, int y, int action, int actionIndex) {
    (void)actionIndex;
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        //第一根手指按下：先登记再判断区域。先 refreshPads 是为了在 onPad 尚未确定时先刷新其它手指的状态。
        GvWTouch* t = allocTouch();
        if (!t) return;
        t->id = pointerId;
        t->x = x;
        t->y = y;
        t->xPrev = x;
        t->yPrev = y;
        t->onPad = false;
        t->active = true;
        g_android.touchX = x;
        g_android.touchY = y;
        g_android.touchDown = true;
        g_android.touchAction = 0;
        ++g_android.pointerCount;
        refreshPads();
        //只有在按下瞬间判断一次是否属于虚拟按键：之后手指滑出按键区也不会转成拖拽，避免操作中途视角乱转。
        t->onPad = hitAnyPad(x, y);
        refreshPads();
    } else if (action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        //后续手指按下：独立判断自己的区域，使其能一边按方向键一边拖视角。
        GvWTouch* t = allocTouch();
        if (!t) return;
        t->id = pointerId;
        t->x = x;
        t->y = y;
        t->xPrev = x;
        t->yPrev = y;
        t->onPad = hitAnyPad(x, y);
        t->active = true;
        ++g_android.pointerCount;
        refreshPads();
    } else if (action == AMOTION_EVENT_ACTION_MOVE) {
        for (int i = 0; i < 8; ++i) {
            GvWTouch& t = g_android.touches[i];
            if (!t.active || t.id != pointerId) continue;
            //只有非按键区域的手指产生视角拖拽位移。
            if (!t.onPad) {
                g_android.dragDX += x - t.xPrev;
                g_android.dragDY += y - t.yPrev;
            }
            t.x = x;
            t.y = y;
            t.xPrev = x;
            t.yPrev = y;
        }
        g_android.touchX = x;
        g_android.touchY = y;
        g_android.touchAction = 1;
        refreshPads();
    } else if (action == AMOTION_EVENT_ACTION_POINTER_UP) {
        //只抬起这一根手指，其它手指继续有效。
        GvWTouch* t = findTouch(pointerId);
        if (t) { t->active = false; --g_android.pointerCount; }
        refreshPads();
    } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
        //最后一根手指离开或事件被系统取消：全部清空，防止残留按键状态。
        for (int i = 0; i < 8; ++i) g_android.touches[i].active = false;
        g_android.pointerCount = 0;
        g_android.touchX = x;
        g_android.touchY = y;
        g_android.touchDown = false;
        g_android.touchAction = 2;
        refreshPads();
    }
}

//清空全部触点状态。用于切后台、原生窗口销毁等"收不到抬起事件"的场合，避免按键卡死。
void GvW_Android_ClearTouches() {
    for (int i = 0; i < 8; ++i) g_android.touches[i].active = false;
    g_android.pointerCount = 0;
    g_android.touchDown = false;
    refreshPads();
}

//接收 Activity 新建或更换的原生窗口。
//窗口对象变化时必须先释放旧引用并销毁旧的 EGL surface，否则新画面会渲染到已失效的窗口上（黑屏或崩溃）。
//持有新窗口时调用 ANativeWindow_acquire 增加引用计数，保证引擎使用期间窗口不会被系统回收。
void GvW_Android_SetNativeWindow(void* win) {
    ANativeWindow* nw = (ANativeWindow*)win;
    if (g_android.nwin == nw) return;
    g_android.nativeWindowChanged = true;
    if (g_android.nwin) {
        ANativeWindow_release(g_android.nwin);
        g_android.nwin = nullptr;
    }
    if (g_android.surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_android.display, g_android.surface);
        g_android.surface = EGL_NO_SURFACE;
    }
    g_android.nwin = nw;
    if (g_android.nwin) ANativeWindow_acquire(g_android.nwin);
}

//清除原生窗口（Activity 销毁窗口时调用）：释放引用并销毁 surface。EGL 上下文保留，等窗口重建后可继续复用。
void GvW_Android_ClearNativeWindow() {
    g_android.nativeWindowChanged = true;
    if (g_android.nwin) {
        ANativeWindow_release(g_android.nwin);
        g_android.nwin = nullptr;
    }
    if (g_android.surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_android.display, g_android.surface);
        g_android.surface = EGL_NO_SURFACE;
    }
}

//安卓没有"创建窗口"的动作：真正的原生窗口由 Activity 异步提供，这里只初始化引擎侧状态。
//title 无用（标题栏由 Java 层管理）；width/height 只是初始估算，真实尺寸以原生窗口为准。
bool GvW_Create(GvWWindow* w, const char* title, int width, int height) {
    (void)title;
    if (!w) return false;
    memset(w, 0, sizeof(*w));
    w->width = width;
    w->height = height;
    w->sys = &g_android;
    w->quit = false;
    return true;
}

//销毁窗口：只关闭 EGL 资源，原生窗口的引用由 Activity 的回调负责释放。
void GvW_Destroy(GvWWindow* w) {
    if (!w) return;
    GvW_GL_Shutdown(w);
    w->sys = nullptr;
}

//每帧轮询：把原生窗口的变化同步到窗口对象，并按最新尺寸刷新虚拟按键的布局缓存。
//安卓事件不走消息队列，而是由 Java 层直接调用 GvW_Android_SetTouch 写入状态，
//因此这里不需要派发消息，只需要确认窗口仍然有效。
bool GvW_Poll(GvWWindow* w) {
    if (!w || !w->sys) return false;
    GvWAndroid* s = (GvWAndroid*)w->sys;
    //原生窗口可能在任意时刻被系统替换（旋转、分屏、切前后台），一旦发现不同就重新取尺寸。
    if (w->nwin != s->nwin) {
        w->nwin = s->nwin;
        s->nativeWindowChanged = true;
    }
    if (s->nativeWindowChanged) {
        s->nativeWindowChanged = false;
        if (w->nwin) {
            w->width = ANativeWindow_getWidth(w->nwin);
            w->height = ANativeWindow_getHeight(w->nwin);
        }
    }
    if (w->width > 0) s->touchWidthCache = w->width;
    if (w->height > 0) s->touchHeightCache = w->height;
    s->touchValid = s->touchAction >= 0;
    return !w->quit;
}

//提交本帧到屏幕。EGL surface 可能尚未就绪（窗口未创建），此时静默跳过。
void GvW_SwapBuffers(GvWWindow* w) {
    if (!w) return;
    GvWAndroid* s = (GvWAndroid*)w->sys;
    if (s->display != EGL_NO_DISPLAY && s->surface != EGL_NO_SURFACE) {
        eglSwapBuffers(s->display, s->surface);
    }
}

//取画面宽度：优先问原生窗口（能反映旋转后的真实尺寸），没有窗口时退回初值。
int GvW_Width(const GvWWindow* w) {
    if (!w) return 0;
    if (w->nwin) return ANativeWindow_getWidth(w->nwin);
    return w->width;
}

//取画面高度，规则同宽度。
int GvW_Height(const GvWWindow* w) {
    if (!w) return 0;
    if (w->nwin) return ANativeWindow_getHeight(w->nwin);
    return w->height;
}

bool GvW_ShouldQuit(const GvWWindow* w) { return !w || w->quit; }

//查询按键状态。安卓没有物理键盘，这里把虚拟按键映射成与桌面一致的语义：
//WASD 对应虚拟方向键，空格对应跳跃键，'T' 对应"屏幕被按住"这一状态，便于脚本用同一套键名判断触摸。
//其他键一律返回 false，因此依赖功能键的操作在安卓上不可用。
bool GvW_IsKeyDown(const GvWWindow* w, int key) {
    if (!w || !w->sys) return false;
    GvWAndroid* s = (GvWAndroid*)w->sys;
    switch (key) {
        case 'W': case 'w': return s->keyW;
        case 'A': case 'a': return s->keyA;
        case 'S': case 's': return s->keyS;
        case 'D': case 'd': return s->keyD;
        case ' ': return s->keyJump;
        case 'T': case 't': return s->touchDown;
        default: return false;
    }
}

//消费"刚触摸"事件。返回 'T' 表示刚按下，返回 'T'+256 表示刚抬起。
//用大于 255 的值表示抬起，是为了不与任何真实虚拟键码冲突，菜单能明确区分两种边沿。
int GvW_ConsumeKeyPress(GvWWindow* w) {
    if (!w || !w->sys) return 0;
    GvWAndroid* s = (GvWAndroid*)w->sys;
    int a = s->touchAction;
    s->touchAction = -1;
    if (a == 0) return 'T';
    if (a == 2) return 'T' + 256;
    return 0;
}

//安卓没有滚轮，恒返回 0。
int GvW_ConsumeScroll(GvWWindow*) { return 0; }

//以触摸点充当鼠标位置，供菜单做命中检测。
void GvW_MousePos(const GvWWindow* w, int* x, int* y) {
    if (!w || !w->sys) { if (x) *x = 0; if (y) *y = 0; return; }
    GvWAndroid* s = (GvWAndroid*)w->sys;
    if (x) *x = s->touchX;
    if (y) *y = s->touchY;
}

//取并清空触屏拖拽位移，作为视角转动量。
void GvW_MouseDelta(const GvWWindow* w, int* dx, int* dy) {
    if (!w || !w->sys) { if (dx) *dx = 0; if (dy) *dy = 0; return; }
    GvWAndroid* s = (GvWAndroid*)w->sys;
    if (dx) { *dx = s->dragDX; s->dragDX = 0; }
    if (dy) { *dy = s->dragDY; s->dragDY = 0; }
}

//触摸屏只有"左键"概念（按下即点按），因此仅 button 0 有效。
bool GvW_MouseDown(const GvWWindow* w, int button) {
    if (!w || !w->sys || button != 0) return false;
    return ((GvWAndroid*)w->sys)->touchDown;
}

//触屏没有"锁定鼠标"的概念，恒返回 false。
bool GvW_GrabMouse(GvWWindow*, bool) { return false; }

//安卓不支持嵌入子窗口，退化为普通创建。
bool GvW_CreateIn(GvWWindow* w, const char* title, int width, int height, void*) {
    return GvW_Create(w, title, width, height);
}
//以下接口在安卓上没有对应概念，做成空实现以保持与桌面端一致的接口形状：
//按键状态由触摸驱动，不存在"全部释放"的显式需求；输入总开关、自动抓鼠标、前台判断、
//光标裁剪与显示、窗口模式（始终全屏）都由系统或 Java 层管理。
void GvW_ReleaseAllKeys(GvWWindow*) {}
void GvW_SetInputEnabled(GvWWindow*, bool) {}
void GvW_SetAutoGrab(GvWWindow*, bool) {}
bool GvW_IsForeground(const GvWWindow*) { return true; }
void GvW_UpdateClipCursorHide(GvWWindow*) {}
void GvW_RestoreCursor(GvWWindow*) {}
void GvW_ApplyMode(GvWWindow*, int) {}
int GvW_Mode(const GvWWindow*) { return 0; }
void GvW_SetResizeCallback(GvWResizeFn) {}

//初始化 OpenGL ES：取默认显示、初始化 EGL、选配置、建上下文、再不断重建 window surface。
//可重复调用：已经就绪时立即返回 true；surface 失效（窗口重建）时会只重建 surface 而保留上下文，
//这样着色器与纹理等 GPU 资源不会因为旋转屏幕而全部丢失。
//优先申请 ES 3.2 上下文（着色器用到 320 es 特性），失败时退回 3.0 客户端版本。
bool GvW_GL_Init(GvWWindow* w) {
    if (!w || !w->sys) return false;
    GvWAndroid* s = (GvWAndroid*)w->sys;
    if (!w->nwin) w->nwin = s->nwin;
    if (s->display != EGL_NO_DISPLAY && s->surface != EGL_NO_SURFACE) return true;
    //窗口还没被系统创建出来，这一帧无法渲染，由上层稍后重试。
    if (!w->nwin) { GVW_LOG("no native window yet"); return false; }

    if (s->display == EGL_NO_DISPLAY) {
        s->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s->display == EGL_NO_DISPLAY) { GVW_LOG("eglGetDisplay failed"); return false; }
        if (!eglInitialize(s->display, nullptr, nullptr)) { GVW_LOG("eglInitialize failed"); return false; }
        //配置要求：可渲染 ES3、可输出到窗口、RGBA 各 8 位、24 位深度、8 位模板。
        const EGLint cfgAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
            EGL_NONE
        };
        EGLConfig cfg = nullptr;
        EGLint numCfg = 0;
        if (!eglChooseConfig(s->display, cfgAttribs, &cfg, 1, &numCfg) || numCfg < 1) {
            GVW_LOG("eglChooseConfig failed");
            return false;
        }
        s->config = cfg;
        //先尝试 ES 3.2（EGL 1.5 的版本属性写法）。
        const EGLint ctxAttribs32[] = {
            EGL_CONTEXT_MAJOR_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 2,
            EGL_NONE
        };
        s->context = eglCreateContext(s->display, cfg, EGL_NO_CONTEXT, ctxAttribs32);
        if (s->context == EGL_NO_CONTEXT) {
            //部分老驱动只认 EGL_CONTEXT_CLIENT_VERSION 写法且最高只到 3.0，退回后仍能运行大部分场景。
            GVW_LOG("gles32 unavailable, fallback 3.0");
            const EGLint ctxAttribs30[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
            s->context = eglCreateContext(s->display, cfg, EGL_NO_CONTEXT, ctxAttribs30);
        }
        if (s->context == EGL_NO_CONTEXT) { GVW_LOG("eglCreateContext failed"); return false; }
    }

    //surface 与原生窗口绑定，窗口一变就必须重建；上下文可以保留，所以只重建 surface。
    if (s->surface != EGL_NO_SURFACE) {
        eglDestroySurface(s->display, s->surface);
        s->surface = EGL_NO_SURFACE;
    }
    s->surface = eglCreateWindowSurface(s->display, s->config, (EGLNativeWindowType)w->nwin, nullptr);
    if (s->surface == EGL_NO_SURFACE) { GVW_LOG("eglCreateWindowSurface failed"); return false; }
    if (!eglMakeCurrent(s->display, s->surface, s->surface, s->context)) { GVW_LOG("eglMakeCurrent failed"); return false; }
    //交换间隔：1 跟随屏幕刷新（省电、无撕裂），0 立即呈现。
    eglSwapInterval(s->display, s->vsync ? 1 : 0);
    //以原生窗口实际尺寸覆盖估算值，保证视口与屏幕一致。
    w->width = ANativeWindow_getWidth(w->nwin);
    w->height = ANativeWindow_getHeight(w->nwin);
    GVW_LOG("gles3 ready %dx%d", w->width, w->height);
    return true;
}

//彻底关闭 EGL：解绑当前上下文后依次销毁 surface、context，最后终止 display。
//注意这里忽略传入的 w 而直接用全局状态，因为安卓端只有一个 EGL 上下文。
void GvW_GL_Shutdown(GvWWindow* w) {
    (void)w;
    GvWAndroid* s = &g_android;
    if (s->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s->surface != EGL_NO_SURFACE) { eglDestroySurface(s->display, s->surface); s->surface = EGL_NO_SURFACE; }
        if (s->context != EGL_NO_CONTEXT) { eglDestroyContext(s->display, s->context); s->context = EGL_NO_CONTEXT; }
        eglTerminate(s->display);
        s->display = EGL_NO_DISPLAY;
    }
}

//取 GLES 扩展函数地址。安卓上没有 wgl 那套机制，统一走 eglGetProcAddress。
void* GvW_GL_Proc(const char* name) { return (void*)eglGetProcAddress(name); }

//设置垂直同步。记录到状态里，是为了在 surface 重建后（eglSwapInterval 只作用于当前 surface）能重新应用。
void GvW_GL_SetVsync(bool vsync) {
    GvWAndroid* s = &g_android;
    s->vsync = vsync;
    if (s->display != EGL_NO_DISPLAY) eglSwapInterval(s->display, vsync ? 1 : 0);
}

#else

//非安卓平台：这些钩子由安卓入口调用，其它平台下做成空实现以保证链接通过。
void GvW_Android_SetNativeWindow(void*) {}
void GvW_Android_ClearNativeWindow() {}
void GvW_Android_SetTouch(int, int, int, int, int) {}
void GvW_Android_ClearTouches() {}

#endif
