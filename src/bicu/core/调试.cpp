#include "Gvtcier.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>

//调试输出开关。关闭时 DBGLN/DBGF 宏的内容不会打印。
static bool g_dbg = false;

//手动开关调试输出，供命令行参数或调试器控制。
void Gvtcier_Debug_SetEnabled(bool on) {
    g_dbg = on;
}

//查询调试输出是否开启。
bool Gvtcier_Debug_Enabled() {
    return g_dbg;
}

//初始化控制台并打开调试输出。发布版（GV_DEBUG 为 0）下整个函数体被编译掉，不产生任何行为。
//发布版一律不开控制台：玩家双击打开游戏时不该弹出一个黑窗口。
//只有调试构建才会附加到父进程控制台，或在没有父控制台时新建一个。
void Gvtcier_Debug_InitConsole() {
#if GV_DEBUG
    g_dbg = true;
    if (!GetConsoleWindow()) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
        }
    } else {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

// 把已经存在的控制台藏起来。用于调试构建从终端启动的场合：日志照样进终端，
// 但不会额外弹窗。发布版没有控制台，这里什么都不做。
void Gvtcier_Debug_HideConsole() {
#if defined(_WIN32)
    HWND h = GetConsoleWindow();
    if (h) ShowWindow(h, SW_HIDE);
#endif
}
