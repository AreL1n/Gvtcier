#include "Gvtcier.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32

#include <imm.h>

//窗口类名。注册后系统按名字定位窗口过程，同一进程内必须唯一。
static const wchar_t* kGvWClass = L"GvWWindowClass";

//Win32 窗口的平台侧状态。引擎只见到不透明的 GvWWindow::sys 指针，具体字段全在这里，
//这样平台细节不会泄漏到上层头文件。
struct GvWWin32 {
    //窗口的设备上下文与 OpenGL 渲染上下文。
    HDC hdc = nullptr;
    HGLRC glctx = nullptr;
    //按键状态表：下标就是虚拟键码，true 表示当前按下。查询是 O(1)，无需向系统反复询问。
    bool keys[256] = {};
    //最近一次按下的键，等待被消费。用于菜单做"刚按下"判断，消费后清零。
    int pendingKey = 0;
    //滚轮累计增量（原始单位，WHEEL_DELTA 为一格）。累加而非立即取整，避免高分辨率滚轮丢事件。
    int scrollAccum = 0;
    //原始输入累积的鼠标位移。用它而不是光标位置差，避免鼠标碰边后无法继续转动视角。
    int rawMouseDX = 0;
    int rawMouseDY = 0;
    //是否已收到过原始鼠标输入。收到后才改用原始输入，否则退回光标居中差。
    bool rawMouseValid = false;
    //左中右三键是否按下，用数组是为了支持按键索引。
    bool mouseDown[3] = {};
    //是否已锁定鼠标（隐藏光标并夹在窗口中心附近）。
    bool grabbing = false;
    //是否为嵌入到父窗口内的子窗口。嵌入模式下不显示光标、不抓鼠标、不响应全屏切换。
    bool embedded = false;
    //窗口是否拥有键盘焦点。
    bool focused = true;
    //输入总开关。关闭时丢弃按键与鼠标按下事件，用于启动画面与脚本接管输入的阶段。
    bool inputEnabled = true;
    //是否允许点击窗口后自动锁定鼠标。暂停菜单里要关掉，否则玩家点不到按钮。
    bool autoGrab = true;
    //输入法是否处于"被临时持有"状态（文本框获得焦点时）。
    bool imeHeld = false;
    //已应用到系统的光标可见状态。记录它是为了配对 ShowCursor 的调用计数，避免多显示/多隐藏。
    bool cursorVisibleApplied = true;
    //已应用"光标在窗口内则隐藏"的状态，同样用于配对计数。
    bool hideCursorInsideApplied = false;
    //窗口客户区尺寸（窗口模式下由 WM_SIZE 维护）。
    int w = 0, h = 0;
    //当前窗口模式，默认 3 即窗口化（见 WindowMode 枚举）。
    int windowMode = 3;
    //进入全屏/最大化之前保存的窗口位置尺寸，用于切回窗口化时还原。
    RECT savedRect = {};
    bool savedRectValid = false;
    //是否处于独占全屏（真正改了显示模式）。仅此状态下退出时需要恢复原显示设置。
    bool exclusive = false;
    //进入独占全屏前保存的显示模式。
    DEVMODEW savedDevMode = {};
    bool savedDevModeValid = false;
};

//进入窗口前系统的键盘布局，用于退出时还原（否则玩家的中文输入法会被换掉）。
static HKL g_prevLayout = nullptr;
//临时加载的英文键盘布局。
static HKL g_enLayout = nullptr;
//窗口尺寸变化回调，由渲染层注册以同步视口。
static GvWResizeFn g_resizeFn = nullptr;

//注册尺寸变化回调。
void GvW_SetResizeCallback(GvWResizeFn fn) { g_resizeFn = fn; }

//强制把窗口切到英文键盘布局。
//原因：游戏内按键监听基于虚拟键码，中文输入法激活时按键会先被输入法拦截（变成 VK_PROCESSKEY），
//导致 WASD 失灵。切到英文布局可避免这个问题。首次调用时记住原布局供退出还原。
static void forceEnglishLayout(HWND h) {
    if (!h) return;
    if (!g_enLayout) g_enLayout = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
    if (!g_enLayout) return;
    if (!g_prevLayout) {
        HWND fg = GetForegroundWindow();
        DWORD tid = fg ? GetWindowThreadProcessId(fg, nullptr) : GetCurrentThreadId();
        g_prevLayout = GetKeyboardLayout(tid);
    }
    PostMessageW(h, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)g_enLayout);
}

//还原进入窗口前的键盘布局并卸载临时加载的英文布局。
//必须在窗口销毁前调用，否则其它程序里玩家的输入法会被留成英文。
static void restoreLayout(HWND h) {
    if (g_prevLayout) {
        ActivateKeyboardLayout(g_prevLayout, KLF_SETFORPROCESS);
        if (h) PostMessageW(h, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)g_prevLayout);
    }
    if (g_enLayout) {
        UnloadKeyboardLayout(g_enLayout);
        g_enLayout = nullptr;
    }
}

//断开窗口与输入法的关联，并把输入法切到英文/字母数字模式。
//目的是让游戏内的按键直接到达窗口而不是被输入法吞掉。IACE_IGNORENOCONTEXT 表示对没有上下文的窗口也生效。
static void detachIme(HWND h) {
    if (!h) return;
    ImmAssociateContextEx(h, nullptr, IACE_IGNORENOCONTEXT);
    HIMC imc = ImmGetContext(h);
    if (imc) {
        ImmSetOpenStatus(imc, FALSE);
        DWORD conv = 0, sent = 0;
        if (ImmGetConversionStatus(imc, &conv, &sent))
            ImmSetConversionStatus(imc, (conv & ~IME_CMODE_LANGUAGE) | IME_CMODE_ALPHANUMERIC, sent);
        ImmReleaseContext(h, imc);
    }
    forceEnglishLayout(h);
}

//恢复窗口与输入法的默认关联。用于引擎 UI 中的文本输入框获得焦点时，让玩家能输入中文。
static void attachIme(HWND h) {
    if (h) ImmAssociateContextEx(h, nullptr, IACE_DEFAULT);
}

//同步系统光标可见状态：锁定鼠标时应隐藏，否则显示。
//只在状态真的变化时调用 ShowCursor，是因为它维护的是内部计数器而非布尔值，
//重复调用会累积计数，导致之后无论怎么调都再也显示/隐藏不了光标。
static void syncCursorVisible(GvWWin32* s) {
    bool wantVisible = !s->grabbing;
    if (wantVisible == s->cursorVisibleApplied) return;
    s->cursorVisibleApplied = wantVisible;
    ShowCursor(wantVisible ? TRUE : FALSE);
}

//窗口过程：所有系统消息的总入口。
//第一件事是把 GvWWindow 指针取回来——创建时（WM_NCCREATE）存进窗口的用户数据区，之后每条消息都从这里读。
//按键与鼠标状态都在这里直接写进 GvWWin32，交由上层的 isKeyDown/MouseDelta 轮询，
//而不是回调式通知，因为游戏主循环是拉取模型，轮询更简单也不会丢帧。
static LRESULT CALLBACK gvwWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    GvWWindow* w = nullptr;
    if (msg == WM_NCCREATE) {
        //创建参数里带着上层给的 GvWWindow 地址，必须在这里保存，之后才能对每条消息定位到它。
        w = (GvWWindow*)((CREATESTRUCTW*)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)w);
    } else {
        w = (GvWWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    GvWWin32* s = w ? (GvWWin32*)w->sys : nullptr;
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            //不在这里销毁任何引擎对象，只置退出标志，让主循环自己走完整清理流程。
            if (w) w->quit = true;
            return 0;
        case WM_ERASEBKGND:
            //返回 1 表示"已擦除"，实际什么都不画：画面每帧由 GPU 完整覆盖，擦背景只会造成闪烁。
            return 1;
        case WM_PAINT: {
            //必须调用 BeginPaint/EndPaint 清掉系统的重绘请求，否则窗口会一直收到 WM_PAINT。
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SIZE:
            //嵌入模式下尺寸由父窗口控制，这里不更新，避免和 SetWindowPos 相互干扰。
            if (w && s && !s->embedded) {
                s->w = LOWORD(lp);
                s->h = HIWORD(lp);
                w->width = s->w;
                w->height = s->h;
                //通知渲染层重建视口与后处理缓冲，尺寸变化不做这一步会导致画面拉伸。
                if (g_resizeFn) g_resizeFn(s->w, s->h);
            }
            return 0;
        case WM_KEYDOWN:
            if (s) {
                if (!s->inputEnabled) return 0;
                //输入法处理中，真实按键被占用，不能当游戏按键记录。
                if (wp == VK_PROCESSKEY) return 0;
                if (wp < 256) {
                    s->keys[wp] = true;
                    s->pendingKey = (int)wp;
                }
            }
            return 0;
        case WM_SYSKEYDOWN:
            //Alt 组合键（如 Alt+F4）走这条消息。lp 的最高位为 1 表示系统自动重复，此时不覆盖 pendingKey，
            //否则按住 Alt 时待消费的按键会被重复键冲掉。
            if (s) {
                if (!s->inputEnabled) return 0;
                if (wp < 256) {
                    s->keys[wp] = true;
                    if (!(lp & 0x40000000)) s->pendingKey = (int)wp;
                }
            }
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            //抬起不检查 inputEnabled：输入关闭期间漏掉的抬起事件也要清除，否则键会一直显示为按下。
            if (s && wp < 256 && wp != VK_PROCESSKEY) s->keys[wp] = false;
            return 0;
        case WM_LBUTTONDOWN:
            if (s) {
                //先拿焦点，保证随后到达的键盘消息都发给本窗口。
                SetFocus(h);
                if (!s->inputEnabled) return 0;
                //点击窗口即锁定鼠标（仅在允许自动锁定且未嵌入时），符合第一人称游戏的常规交互。
                if (!s->grabbing && !s->embedded && s->autoGrab) GvW_GrabMouse(w, true);
                s->mouseDown[0] = true;
                //同时写入按键表，让上层能用统一的 isKeyDown(VK_LBUTTON) 查询。
                s->keys[VK_LBUTTON] = true;
            }
            return 0;
        case WM_LBUTTONUP:
            if (s) { s->mouseDown[0] = false; s->keys[VK_LBUTTON] = false; }
            return 0;
        case WM_RBUTTONDOWN:
            if (s) {
                SetFocus(h);
                if (!s->inputEnabled) return 0;
                if (!s->grabbing && !s->embedded && s->autoGrab) GvW_GrabMouse(w, true);
                s->mouseDown[1] = true;
                s->keys[VK_RBUTTON] = true;
            }
            return 0;
        case WM_RBUTTONUP:
            if (s) { s->mouseDown[1] = false; s->keys[VK_RBUTTON] = false; }
            return 0;
        case WM_MBUTTONDOWN: if (s) s->mouseDown[2] = true; return 0;
        case WM_MBUTTONUP:   if (s) s->mouseDown[2] = false; return 0;
        case WM_MOUSEWHEEL:
            //累加原始增量而非直接换算成格数：一格 WHEEL_DELTA（120），高精度滚轮会有小于一格的事件。
            if (s) s->scrollAccum += GET_WHEEL_DELTA_WPARAM(wp);
            return 0;
        case WM_INPUT: {
            //原始输入：绕过光标加速与屏幕边界，拿到真实鼠标位移，是第一人称视角的标准做法。
            if (!s) return 0;
            RAWINPUT raw;
            UINT sz = sizeof(raw);
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &raw, &sz, sizeof(RAWINPUTHEADER)) == (UINT)-1)
                return 0;
            if (raw.header.dwType == RIM_TYPEMOUSE) {
                const RAWMOUSE& ms = raw.data.mouse;
                //绝对坐标模式（某些远程桌面/虚拟机）下 lLastX 不是位移，直接忽略，改用光标差作为后备。
                if ((ms.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                    s->rawMouseDX += ms.lLastX;
                    s->rawMouseDY += ms.lLastY;
                    s->rawMouseValid = true;
                }
                return 0;
            }
            //原始键盘输入只用于区分左右修饰键：虚拟键码只能报 VK_SHIFT，无法区分左右。
            if (raw.header.dwType != RIM_TYPEKEYBOARD) return 0;
            const RAWKEYBOARD& kb = raw.data.keyboard;
            //VKey 为 0xFF（伪键）或 0 是无效扫描码，丢弃。
            if (kb.VKey == 0xFF || kb.VKey == 0) return 0;
            bool down = (kb.Flags & RI_KEY_BREAK) == 0;
            bool e0 = (kb.Flags & RI_KEY_E0) != 0;
            bool e1 = (kb.Flags & RI_KEY_E1) != 0;
            int vk = 0;
            if (kb.VKey == VK_SHIFT) {
                //用扫描码反查具体是左 Shift 还是右 Shift。
                UINT sc = MapVirtualKeyW(kb.MakeCode, MAPVK_VSC_TO_VK_EX);
                vk = (int)sc;
            } else if (kb.VKey == VK_CONTROL) {
                vk = e0 ? VK_RCONTROL : VK_LCONTROL;
            } else if (kb.VKey == VK_MENU) {
                vk = e0 ? VK_RMENU : VK_LMENU;
            } else if (kb.VKey == VK_RETURN && e0) {
                //小键盘回车带 E0 标志，单独映射成 VK_SEPARATOR，与主回车区分。
                vk = VK_SEPARATOR;
            } else if (kb.VKey == VK_DELETE && !e0) {
                //不带 E0 的 Delete 是小键盘的"小数点"，不是真正的 Delete，忽略以免误触发。
                return 0;
            } else if (e1) {
                //E1 是 Pause/Break 的扩展前缀，无需处理。
                return 0;
            } else {
                vk = kb.VKey;
            }
            if (vk > 0 && vk < 256) {
                //输入关闭时仍记录"抬起"，否则关闭期间的按键会一直卡在按下状态。
                if (s->inputEnabled || !down) {
                    s->keys[vk] = down;
                    if (down) s->pendingKey = vk;
                }
            }
            return 0;
        }
        case WM_SETFOCUS:
            if (w && s) {
                s->focused = true;
                w->focused = true;
                //重新获得焦点时主动锁鼠标，玩家从别的窗口切回来不必再点一下。
                if (!s->embedded && s->inputEnabled && s->autoGrab) GvW_GrabMouse(w, true);
                //除文本框正在用输入法外，其余情况都断开输入法。
                if (!s->imeHeld) detachIme(h);
            }
            return 0;
        case WM_KILLFOCUS:
            if (w && s) {
                //失去焦点必须清空按键状态：焦点移走后收不到抬起事件，不清会一直"按住"某个键，
                //切回来时角色持续奔跑。
                s->focused = false;
                w->focused = false;
                GvW_ReleaseAllKeys(w);
                GvW_GrabMouse(w, false);
            }
            return 0;
        case WM_COMMAND: {
            //这里只处理子控件（文本框）的焦点变化：获得焦点时挂回输入法以便输入中文，
            //失去焦点时再断开，恢复游戏按键。
            if (!s) break;
            HWND ctl = (HWND)lp;
            if (!ctl) break;
            WORD code = HIWORD(wp);
            if (code == EN_SETFOCUS) {
                s->imeHeld = true;
                attachIme(w->hwnd);
            } else if (code == EN_KILLFOCUS) {
                s->imeHeld = false;
                detachIme(w->hwnd);
            }
            break;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

//创建窗口的公共实现。parentHwnd 非空时创建为嵌入子窗口，否则创建独立顶层窗口。
//title 为 UTF-8 标题，width/height 为客户区期望尺寸。
//顶层窗口按客户区尺寸反推外框尺寸，并居屏幕居中；嵌入窗口则铺满父窗口客户区。
//返回 false 表示窗口创建失败，此时内部状态已回收，可安全重试。
static bool createWindowCommon(GvWWindow* w, const char* title, int width, int height, void* parentHwnd) {
    if (!w) return false;
    memset(w, 0, sizeof(*w));
    w->width = width;
    w->height = height;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {};
    //CS_OWNDC：每个窗口独占一个设备上下文。OpenGL 上下文必须绑定在稳定的 DC 上，共享 DC 会出问题。
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = gvwWndProc;
    wc.hInstance = inst;
    wc.hCursor = (HCURSOR)LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = kGvWClass;
    //重复注册同名类会失败，这里不检查返回值：多窗口创建时第二次注册本来就该失败且无害。
    RegisterClassW(&wc);
    //AdjustWindowRect 把"客户区尺寸"换算成"含标题栏与边框的外框尺寸"，
    //否则实际可绘制区域会比请求的小一圈，视口与窗口尺寸对不上。
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    wchar_t wtitle[256] = {};
    MultiByteToWideChar(CP_UTF8, 0, title ? title : "GvW", -1, wtitle, 255);
    GvWWin32* s = new GvWWin32();
    w->sys = s;
    s->embedded = (parentHwnd != nullptr);
    s->w = width;
    s->h = height;
    //顶层窗口居中显示；嵌入窗口位置交给 SetParent 之后的 SetWindowPos 决定。
    w->hwnd = CreateWindowW(kGvWClass, wtitle,
                            WS_OVERLAPPEDWINDOW | (parentHwnd ? 0 : WS_VISIBLE),
                            parentHwnd ? CW_USEDEFAULT : (sw - ww) / 2,
                            parentHwnd ? CW_USEDEFAULT : (sh - wh) / 2,
                            ww, wh,
                            nullptr, nullptr, inst, w);
    if (!w->hwnd) { delete s; w->sys = nullptr; return false; }
    //创建后立即断开输入法，保证第一次按键就能被游戏收到。
    detachIme(w->hwnd);
    if (parentHwnd) {
        //嵌入模式：改成子窗口样式并铺满父客户区。
        SetParent(w->hwnd, (HWND)parentHwnd);
        SetWindowLongW(w->hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE);
        RECT pr;
        GetClientRect((HWND)parentHwnd, &pr);
        int pw = pr.right - pr.left, ph = pr.bottom - pr.top;
        //父窗口尺寸异常（尚未布局，宽高小于 16）时退回到请求尺寸，避免创建出零尺寸视口。
        if (pw < 16 || ph < 16) { pw = width; ph = height; }
        SetWindowPos(w->hwnd, nullptr, 0, 0, pw, ph,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        s->w = pw;
        s->h = ph;
        w->width = pw;
        w->height = ph;
        SetFocus(w->hwnd);
    }
    //注册原始输入设备：第一项是鼠标（UsagePage 0x01 / Usage 0x06），第二项是键盘（Usage 0x02）。
    //RIDEV_INPUTSINK 表示窗口即使不在前台也能收到输入——第一人称游戏切到后台再切回来时不会丢状态。
    //注意：不注册键盘的原始输入时，左右 Shift 无法区分。
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01;
    rid[0].usUsage = 0x06;
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = w->hwnd;
    rid[1].usUsagePage = 0x01;
    rid[1].usUsage = 0x02;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = w->hwnd;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    return true;
}

//创建独立顶层窗口并显示。失败返回 false。
bool GvW_Create(GvWWindow* w, const char* title, int width, int height) {
    if (!createWindowCommon(w, title, width, height, nullptr)) return false;
    //CreateWindowW 已带 WS_VISIBLE，这里再 ShowWindow 是为了让窗口立即完成首次显示并拿到焦点。
    ShowWindow(w->hwnd, SW_SHOW);
    return true;
}

//在指定父窗口内创建嵌入子窗口。父窗口销毁时子窗口随之销毁。
bool GvW_CreateIn(GvWWindow* w, const char* title, int width, int height, void* parentHwnd) {
    return createWindowCommon(w, title, width, height, parentHwnd);
}

//销毁窗口并恢复所有被本窗口修改过的系统状态。
//顺序很重要：先解锁鼠标、解除光标裁剪，再退出独占全屏恢复显示模式，然后还原输入法与键盘布局，
//最后才关闭 OpenGL 上下文与窗口本身；顺序错了会留下仍被裁剪的光标或错误的显示分辨率。
void GvW_Destroy(GvWWindow* w) {
    if (!w) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (s) {
        s->grabbing = false;
        ClipCursor(nullptr);
        if (s->exclusive) {
            s->exclusive = false;
            ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
            if (s->savedDevModeValid) {
                s->savedDevMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
                ChangeDisplaySettingsExW(nullptr, &s->savedDevMode, nullptr, CDS_FULLSCREEN, nullptr);
            }
        }
        if (w->hwnd) attachIme(w->hwnd);
        restoreLayout(w->hwnd);
    }
    GvW_GL_Shutdown(w);
    if (w->hwnd) DestroyWindow(w->hwnd);
    if (s) delete s;
    w->sys = nullptr;
    w->hwnd = nullptr;
}

//抽取并派发当前线程的全部待处理消息。返回 false 表示窗口已要求退出。
//用 PeekMessage 而非 GetMessage：游戏主循环必须每帧返回，不能阻塞等消息。
//遇到 WM_QUIT 立即置退出标志并停止派发，让主循环尽快走清理流程。
bool GvW_Poll(GvWWindow* w) {
    if (!w) return false;
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { w->quit = true; break; }
        //TranslateMessage 负责把按键转成字符消息（输入框需要），DispatchMessage 才真正调窗口过程。
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !w->quit;
}

//交换前后缓冲，把本帧画好的内容显示出来。
void GvW_SwapBuffers(GvWWindow* w) {
    if (!w || !w->sys) return;
    SwapBuffers(((GvWWin32*)w->sys)->hdc);
}

//取窗口客户区宽度。直接问系统而不是缓存的 s->w，保证与实际可绘制区域一致。
int GvW_Width(const GvWWindow* w) {
    if (!w || !w->hwnd) return 0;
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    return rc.right - rc.left;
}

//取窗口客户区高度。
int GvW_Height(const GvWWindow* w) {
    if (!w || !w->hwnd) return 0;
    RECT rc;
    GetClientRect(w->hwnd, &rc);
    return rc.bottom - rc.top;
}

//窗口是否应当退出。空指针也视为应退出，便于调用方简化判空。
bool GvW_ShouldQuit(const GvWWindow* w) { return !w || w->quit; }

//取当前窗口模式（0 独占全屏 / 1 无边框全屏 / 2 最大化 / 3 窗口化）。空指针时返回 3。
int GvW_Mode(const GvWWindow* w) {
    if (!w || !w->sys) return 3;
    return ((GvWWin32*)w->sys)->windowMode;
}

//查询按键是否处于按下状态。key 为虚拟键码，越界返回 false。
bool GvW_IsKeyDown(const GvWWindow* w, int key) {
    if (!w || !w->sys || key < 0 || key >= 256) return false;
    return ((GvWWin32*)w->sys)->keys[key];
}

//取出并清空"最近按下的键"。
//一次性消费的语义让菜单能可靠地做"刚按下"判断：同一个按键不会被两次查询重复响应；
//没有待消费按键时返回 0。
int GvW_ConsumeKeyPress(GvWWindow* w) {
    if (!w || !w->sys) return 0;
    GvWWin32* s = (GvWWin32*)w->sys;
    int k = s->pendingKey;
    s->pendingKey = 0;
    return k;
}

//取出滚轮格数并清零累计值。不足一格的零头保留在累计器里，等凑够再上报，不丢事件。
int GvW_ConsumeScroll(GvWWindow* w) {
    if (!w || !w->sys) return 0;
    GvWWin32* s = (GvWWin32*)w->sys;
    int v = s->scrollAccum;
    s->scrollAccum = 0;
    return v / WHEEL_DELTA;
}

//取鼠标在客户区内的坐标（左上角为原点），供菜单做命中检测。
//先把系统坐标转到窗口坐标再输出，这样多显示器、负坐标屏也能正确工作。
void GvW_MousePos(const GvWWindow* w, int* x, int* y) {
    if (x) *x = 0;
    if (y) *y = 0;
    if (!w || !w->hwnd) return;
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(w->hwnd, &p);
    if (x) *x = p.x;
    if (y) *y = p.y;
}

//取本帧鼠标位移并清零累计值，用于转视角。
//只在锁定状态（grabbing）下有效，否则返回 0 且不消耗事件。
//优先用原始输入位移；若驱动一直没给原始输入（rawMouseValid 为假），退回"光标相对窗口中心的偏移"方案：
//每帧把光标拉回中心，用偏移量当位移。两种方式都保证位移没有屏幕边界限制。
//窗口失去前台时会主动解锁鼠标，避免玩家在别的程序里仍然被锁住光标。
void GvW_MouseDelta(const GvWWindow* w, int* dx, int* dy) {
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (!s->grabbing) return;
    if (!GvW_IsForeground(w)) {
        GvW_GrabMouse((GvWWindow*)w, false);
        return;
    }
    if (s->rawMouseValid) {
        if (dx) { *dx = s->rawMouseDX; s->rawMouseDX = 0; }
        if (dy) { *dy = s->rawMouseDY; s->rawMouseDY = 0; }
        return;
    }
    RECT cr;
    GetClientRect(w->hwnd, &cr);
    POINT center = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
    ClientToScreen(w->hwnd, &center);
    POINT cur;
    GetCursorPos(&cur);
    if (dx) *dx = cur.x - center.x;
    if (dy) *dy = cur.y - center.y;
    SetCursorPos(center.x, center.y);
}

//查询鼠标按键是否按下。button：0 左键、1 右键、2 中键，越界返回 false。
bool GvW_MouseDown(const GvWWindow* w, int button) {
    if (!w || !w->sys || button < 0 || button >= 3) return false;
    return ((GvWWin32*)w->sys)->mouseDown[button];
}

//锁定或释放鼠标。grab 为 true 表示进入第一人称模式：隐藏光标、把光标移到窗口中心并用 8x8 像素的
//裁剪区域把它关在中心附近。这个 8 像素的余量是让光标在小幅移动时不至于"顶住"裁剪边界而丢位移，
//同时又足以让 Shift+Esc（独占全屏）等系统组合键无法把鼠标移出窗口。
//输入被禁用时拒绝锁定，避免在启动画面或脚本接管期间抢鼠标。
//返回 true 表示当前已处于期望状态（包括本来就一致的情况）。
bool GvW_GrabMouse(GvWWindow* w, bool grab) {
    if (!w || !w->sys) return false;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (grab && !s->inputEnabled) return false;
    if (grab == s->grabbing) return true;
    s->grabbing = grab;
    //切换状态时清空累积位移：否则上一状态残留的位移会让视角突然跳一下。
    s->rawMouseDX = 0;
    s->rawMouseDY = 0;
    syncCursorVisible(s);
    if (grab && w->hwnd) {
        RECT cr;
        GetClientRect(w->hwnd, &cr);
        POINT c = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
        ClientToScreen(w->hwnd, &c);
        SetCursorPos(c.x, c.y);
        RECT clip = { c.x - 8, c.y - 8, c.x + 8, c.y + 8 };
        ClipCursor(&clip);
    } else {
        //传 nullptr 解除裁剪，否则光标会一直被锁在上一处。
        ClipCursor(nullptr);
    }
    return true;
}

//清空所有按键状态与待消费按键。
//用于失去焦点、关闭输入等"收不到抬起事件"的场合，防止键位卡在按下状态。
void GvW_ReleaseAllKeys(GvWWindow* w) {
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    memset(s->keys, 0, sizeof(s->keys));
    s->pendingKey = 0;
}

//启用或禁用输入。禁用时立即清空按键并释放鼠标，保证切换到"不可操作"状态时不会残留任何按下状态。
void GvW_SetInputEnabled(GvWWindow* w, bool enabled) {
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    s->inputEnabled = enabled;
    if (!enabled) {
        GvW_ReleaseAllKeys(w);
        GvW_GrabMouse(w, false);
    }
}

//设置是否允许点击窗口自动锁定鼠标。关闭时会立即解锁，让玩家能正常操作菜单与其它窗口。
void GvW_SetAutoGrab(GvWWindow* w, bool on) {
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    s->autoGrab = on;
    if (!on) GvW_GrabMouse(w, false);
}

//判断窗口是否处于前台可交互状态。
//同时检查两点：本窗口的顶层祖先是否为前台窗口，以及键盘焦点是否仍在本窗口的顶层窗口内。
//两者都满足才算"前台"——只看前台窗口会漏掉焦点被其它窗口抢走的情况（例如弹出的系统对话框）。
bool GvW_IsForeground(const GvWWindow* w) {
    if (!w || !w->hwnd) return false;
    HWND root = GetAncestor(w->hwnd, GA_ROOT);
    if (!root || GetForegroundWindow() != root) return false;
    HWND focus = GetFocus();
    if (focus && GetAncestor(focus, GA_ROOT) != root) return false;
    return true;
}

//根据光标是否落在窗口客户区内，自动隐藏或显示光标。
//作用是在窗口化模式下让光标进入游戏区域时消失、移出时恢复，既不挡视野也不影响操作其它窗口。
//嵌入模式与锁定模式下不参与此逻辑（锁定时已由 syncCursorVisible 管理），此时强制恢复光标可见。
//同样用 hideCursorInsideApplied 记录已应用状态，保证 ShowCursor 调用成对，不会破坏内部计数。
void GvW_UpdateClipCursorHide(GvWWindow* w) {
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (!w->hwnd || s->embedded || s->grabbing) {
        if (s->hideCursorInsideApplied) {
            s->hideCursorInsideApplied = false;
            ShowCursor(TRUE);
        }
        return;
    }
    POINT cur;
    if (!GetCursorPos(&cur)) return;
    RECT cr;
    GetClientRect(w->hwnd, &cr);
    POINT tl = { cr.left, cr.top };
    POINT br = { cr.right, cr.bottom };
    ClientToScreen(w->hwnd, &tl);
    ClientToScreen(w->hwnd, &br);
    bool inside = cur.x >= tl.x && cur.x < br.x && cur.y >= tl.y && cur.y < br.y;
    if (inside == s->hideCursorInsideApplied) return;
    s->hideCursorInsideApplied = inside;
    ShowCursor(inside ? FALSE : TRUE);
}

//强制恢复光标可见。供切菜单、退出游戏等必须操作光标的时机调用，比 UpdateClipCursorHide 更直接。
//仅在确实处于"已隐藏"状态时才 ShowCursor(TRUE)，保证计数配对。
void GvW_RestoreCursor(GvWWindow* w) {
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (!s->hideCursorInsideApplied) return;
    s->hideCursorInsideApplied = false;
    ShowCursor(TRUE);
}

//退出独占全屏：先把显示设置临时还原（ChangeDisplaySettingsExW 传 nullptr 表示回退到注册表里的默认模式），
//再应用之前保存的具体显示模式。两步都要做，只做第二步在某些驱动上无法恢复桌面分辨率。
static void leaveExclusive(GvWWindow* w, GvWWin32* s) {
    if (!s->exclusive) return;
    s->exclusive = false;
    ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    if (s->savedDevModeValid) {
        s->savedDevMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        ChangeDisplaySettingsExW(nullptr, &s->savedDevMode, nullptr, CDS_FULLSCREEN, nullptr);
        s->savedDevModeValid = false;
    }
}

//应用窗口模式。mode 取值见 WindowMode：0 独占全屏、1 无边框全屏、2 最大化、3 窗口化；非法值按窗口化处理。
//嵌入窗口不参与模式切换（尺寸由父窗口决定）。
//切到窗口化之前会保存当前窗口位置尺寸，之后切回来时还原；
//切进全屏会先退出可能存在的独占状态，避免显示设置被叠加修改。
//最后如果鼠标处于锁定状态，重新把光标夹回窗口中心——窗口尺寸变了，旧的裁剪区域已经无效。
void GvW_ApplyMode(GvWWindow* w, int mode) {
    if (!w || !w->sys || !w->hwnd) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (s->embedded) return;
    if (mode < 0 || mode > 3) mode = 3;
    //离开独占全屏只对非独占模式做，否则会刚设置完又立刻撤销。
    if (mode != 0) leaveExclusive(w, s);
    //窗口化与最大化之间切换时，样式差异不大，但都要重新走一遍布局，因此不提前返回。
    if (mode == s->windowMode && mode != 0 && mode != 1) return;
    if (s->windowMode == 3 && !s->savedRectValid) {
        //只在从窗口化切出去时记录一次，避免连续切换时把全屏尺寸当成还原目标。
        RECT r;
        if (GetWindowRect(w->hwnd, &r)) {
            s->savedRect = r;
            s->savedRectValid = true;
        }
    }
    s->windowMode = mode;
    if (mode == 0 || mode == 1) {
        //两种全屏都先把窗口改成无边框弹窗，铺满当前所在显示器（而非主显示器）。
        HMONITOR mon = MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(mon, &mi);
        const RECT& mr = mi.rcMonitor;
        SetWindowLongW(w->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(w->hwnd, HWND_TOPMOST, mr.left, mr.top,
                     mr.right - mr.left, mr.bottom - mr.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        if (mode == 0) {
            //独占全屏：真正切换显示模式到显示器当前分辨率、32 位色。
            //只在切换成功后才标记 exclusive，失败时保持窗口无边框全屏即可，不要假装改过。
            DEVMODEW dm = {};
            dm.dmSize = sizeof(dm);
            dm.dmPelsWidth = (DWORD)(mr.right - mr.left);
            dm.dmPelsHeight = (DWORD)(mr.bottom - mr.top);
            dm.dmBitsPerPel = 32;
            dm.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
            //首次进入前保存原显示模式，供之后还原；已保存过就不覆盖，避免把全屏模式存成"原始模式"。
            if (!s->savedDevModeValid) {
                s->savedDevMode.dmSize = sizeof(s->savedDevMode);
                if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &s->savedDevMode))
                    s->savedDevModeValid = true;
            }
            LONG r = ChangeDisplaySettingsExW(nullptr, &dm, nullptr, CDS_FULLSCREEN, nullptr);
            s->exclusive = (r == DISP_CHANGE_SUCCESSFUL);
        }
    } else if (mode == 2) {
        //最大化：恢复带边框样式后让系统最大化，便于玩家用任务栏切换。
        SetWindowLongW(w->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(w->hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(w->hwnd, SW_MAXIMIZE);
    } else {
        //窗口化：先还原边框与正常状态，再按保存的位置尺寸精确摆回原处。
        SetWindowLongW(w->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(w->hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(w->hwnd, SW_RESTORE);
        if (s->savedRectValid) {
            int ww = s->savedRect.right - s->savedRect.left;
            int wh = s->savedRect.bottom - s->savedRect.top;
            SetWindowPos(w->hwnd, nullptr, s->savedRect.left, s->savedRect.top, ww, wh,
                         SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }
    if (s->grabbing) {
        //尺寸变了，旧的裁剪矩形失效，重新计算并清空累积位移，避免视角跳变。
        RECT cr;
        GetClientRect(w->hwnd, &cr);
        POINT c = { (cr.right - cr.left) / 2, (cr.bottom - cr.top) / 2 };
        ClientToScreen(w->hwnd, &c);
        SetCursorPos(c.x, c.y);
        s->rawMouseDX = 0;
        s->rawMouseDY = 0;
        RECT clip = { c.x - 8, c.y - 8, c.x + 8, c.y + 8 };
        ClipCursor(&clip);
    }
}

//初始化 OpenGL：先设像素格式，再用临时上下文获取 CreateContextAttribsARB 入口，
//最后用该入口创建真正要用的 4.6 核心模式上下文。
//之所以绕一圈用临时上下文：wglCreateContext 只能建到驱动默认的旧版本，要指定版本必须走这个扩展函数，
//而扩展函数地址必须先有当前上下文才能取到。属性 0x2091 是 GL_MAJOR_VERSION，0x2092 是 GL_MINOR_VERSION。
//失败时返回 false，已申请的资源不在这里回收，由 GvW_GL_Shutdown 统一处理。
bool GvW_GL_Init(GvWWindow* w) {
    if (!w || !w->hwnd || !w->sys) return false;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (s->glctx) return true;
    //像素格式：双缓冲、32 位 RGBA、24 位深度、8 位模板。模板缓冲是某些后处理与遮罩需要的最低配置。
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    s->hdc = GetDC(w->hwnd);
    int pf = ChoosePixelFormat(s->hdc, &pfd);
    if (!pf || !SetPixelFormat(s->hdc, pf, &pfd)) { printf("[gvw] pixel format failed\n"); return false; }
    typedef HGLRC (APIENTRY *PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);
    //临时上下文只用来取扩展函数地址，取到后立即销毁。
    HGLRC dummy = wglCreateContext(s->hdc);
    if (!dummy || !wglMakeCurrent(s->hdc, dummy)) { printf("[gvw] dummy ctx failed\n"); return false; }
    auto createCtx = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
    HGLRC core = nullptr;
    if (createCtx) {
        //请求 GL 4.6 核心模式（属性对以 0 结尾）。
        const int attribs[] = { 0x2091, 4, 0x2092, 6, 0 };
        core = createCtx(s->hdc, nullptr, attribs);
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummy);
    if (!core) { printf("[gvw] gl 4.6 core ctx failed\n"); return false; }
    s->glctx = core;
    wglMakeCurrent(s->hdc, core);
    //默认开垂直同步：避免无上限空转导致显卡满载与画面撕裂。之后可由设置关闭。
    GvW_GL_SetVsync(true);
    printf("[gvw] gl 4.6 core ready\n");
    return true;
}

//销毁 OpenGL 上下文与设备上下文。先解绑再删除，这是 Windows GL 的固定顺序。
void GvW_GL_Shutdown(GvWWindow* w) {
    if (!w || !w->sys) return;
    GvWWin32* s = (GvWWin32*)w->sys;
    if (s->glctx) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(s->glctx); s->glctx = nullptr; }
    if (s->hdc && w->hwnd) { ReleaseDC(w->hwnd, s->hdc); s->hdc = nullptr; }
}

//按名字取 OpenGL 扩展函数地址。返回值需由调用方按正确的函数签名转换。
void* GvW_GL_Proc(const char* name) { return (void*)wglGetProcAddress(name); }

//设置垂直同步。通过 WGL 交换间隔扩展实现：1 表示与屏幕刷新同步，0 表示由驱动尽快呈现。
//驱动不提供该扩展时静默忽略，退化为驱动默认行为。
void GvW_GL_SetVsync(bool vsync) {
    typedef BOOL (APIENTRY *PFNWGLSWAPINTERVALEXTPROC)(int);
    auto f = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (f) f(vsync ? 1 : 0);
}

#else

//非 Windows 平台的空实现：保证同一套接口在其它系统上仍能链接通过，
//但所有功能都不可用（创建失败、查询返回空值），由上层视为"平台不支持"处理。
bool GvW_Create(GvWWindow*, const char*, int, int) { return false; }
bool GvW_CreateIn(GvWWindow*, const char*, int, int, void*) { return false; }
void GvW_Destroy(GvWWindow*) {}
bool GvW_Poll(GvWWindow*) { return false; }
void GvW_SwapBuffers(GvWWindow*) {}
int GvW_Width(const GvWWindow*) { return 0; }
int GvW_Height(const GvWWindow*) { return 0; }
bool GvW_ShouldQuit(const GvWWindow*) { return true; }
bool GvW_IsKeyDown(const GvWWindow*, int) { return false; }
int GvW_ConsumeKeyPress(GvWWindow*) { return 0; }
int GvW_ConsumeScroll(GvWWindow*) { return 0; }
void GvW_MousePos(const GvWWindow*, int*, int*) {}
void GvW_MouseDelta(const GvWWindow*, int*, int*) {}
bool GvW_MouseDown(const GvWWindow*, int) { return false; }
bool GvW_GrabMouse(GvWWindow*, bool) { return false; }
void GvW_ReleaseAllKeys(GvWWindow*) {}
void GvW_SetInputEnabled(GvWWindow*, bool) {}
void GvW_SetAutoGrab(GvWWindow*, bool) {}
bool GvW_IsForeground(const GvWWindow*) { return false; }
void GvW_UpdateClipCursorHide(GvWWindow*) {}
void GvW_RestoreCursor(GvWWindow*) {}
void GvW_ApplyMode(GvWWindow*, int) {}
void GvW_SetResizeCallback(GvWResizeFn) {}
bool GvW_GL_Init(GvWWindow*) { return false; }
void GvW_GL_Shutdown(GvWWindow*) {}
void* GvW_GL_Proc(const char*) { return nullptr; }
void GvW_GL_SetVsync(bool) {}

#endif
