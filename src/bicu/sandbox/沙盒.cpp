#include "Gvtcier.h"
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <ctime>

#pragma comment(lib, "gdiplus.lib")

// 沙盒调试器（产物 dist\Gvt调试器.exe）的实现。
// 它不是引擎的一部分，而是开发期工具：启动一个宿主进程跑游戏、捕获它的标准输出、
// 显示日志窗口，并在崩溃时收集转储。
// 运行方式见项目规则：dist\Gvt调试器.exe -run dist\main.exe 游戏包路径
namespace {

// 生成 20261024_153012 这样的时间戳，用于命名日志与转储文件
std::string TimeStamp() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_s(&lt, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &lt);
    return buf;
}

std::string JoinDir(const std::string& dir, const char* name) {
    return dir + "\\" + name;
}

bool EnsureDir(const std::string& dir) {
    DWORD a = GetFileAttributesA(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryA(dir.c_str(), NULL) != 0;
}

std::string FullPath(const std::string& p) {
    char buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameA(p.c_str(), sizeof(buf), buf, NULL);
    if (n == 0 || n >= sizeof(buf)) return p;
    return buf;
}

struct WinFindCtx {
    DWORD pid = 0;
    HWND found = NULL;
};

BOOL CALLBACK FindMainWindowProc(HWND h, LPARAM lp) {
    WinFindCtx* c = (WinFindCtx*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != c->pid) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindow(h, GW_OWNER) != NULL) return TRUE;
    RECT r;
    if (!GetWindowRect(h, &r)) return TRUE;
    if (r.right - r.left < 64 || r.bottom - r.top < 64) return TRUE;
    c->found = h;
    return FALSE;
}

HWND FindMainWndByPid(DWORD pid) {
    WinFindCtx c;
    c.pid = pid;
    EnumWindows(FindMainWindowProc, (LPARAM)&c);
    return c.found;
}

bool SendClickToWindow(HWND h, float u, float v) {
    if (!h) return false;
    RECT cr{};
    if (!GetClientRect(h, &cr)) return false;
    int x = (int)(u * (float)(cr.right - cr.left));
    int y = (int)(v * (float)(cr.bottom - cr.top));
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    LPARAM lp = MAKELPARAM(x, y);
    PostMessageA(h, WM_MOUSEMOVE, 0, lp);
    Sleep(30);
    PostMessageA(h, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(60);
    PostMessageA(h, WM_LBUTTONUP, 0, lp);
    char line[160];
    snprintf(line, sizeof(line), "[click] client %ldx%ld at (%d,%d)\n",
             (long)(cr.right - cr.left), (long)(cr.bottom - cr.top), x, y);
    printf("%s", line);
    return true;
}

bool SendKeyToWindow(HWND h, UINT vk) {
    if (!h) return false;
    PostMessageA(h, WM_KEYDOWN, vk, 0);
    Sleep(40);
    PostMessageA(h, WM_KEYUP, vk, 0);
    return true;
}

bool PngEncoderClsid(CLSID& out) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
    std::vector<unsigned char> buf(size);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(info[i].MimeType, L"image/png") == 0) { out = info[i].Clsid; return true; }
    }
    return false;
}

bool CaptureWindowPng(DWORD pid, const std::string& outPath) {
    static bool gpReady = false;
    if (!gpReady) {
        Gdiplus::GdiplusStartupInput in;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &in, NULL) != Gdiplus::Ok) return false;
        gpReady = true;
    }
    WinFindCtx c;
    c.pid = pid;
    EnumWindows(FindMainWindowProc, (LPARAM)&c);
    if (!c.found) return false;

    RECT r;
    if (!GetWindowRect(c.found, &r)) return false;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return false;

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = PrintWindow(c.found, mem, 2);
    if (!ok) BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    CLSID clsid;
    bool saved = false;
    if (PngEncoderClsid(clsid)) {
        Gdiplus::Bitmap gb(bmp, NULL);
        std::wstring wp = Gvtcier_W32_ToWide(outPath.c_str());
        saved = gb.Save(wp.c_str(), &clsid, NULL) == Gdiplus::Ok;
    }
    DeleteObject(bmp);
    return saved;
}

std::string DescribeExit(uint32_t code, bool& crashed) {
    crashed = false;
    if (code == 0) return "正常退出";
    struct Entry { uint32_t code; const char* desc; };
    static const Entry kMap[] = {
        { 0xC0000005u, "访问违例 (空指针或越界读写)" },
        { 0xC0000017u, "内存耗尽" },
        { 0xC000001Du, "非法指令" },
        { 0xC0000094u, "整数除零" },
        { 0xC00000FDu, "栈溢出 (递归过深或局部变量过大)" },
        { 0xC0000374u, "堆损坏 (重复释放或越界写堆)" },
        { 0xC0000409u, "栈缓冲区溢出或安全校验失败" },
        { 0xC0000135u, "找不到依赖 DLL" },
        { 0xC0000139u, "DLL 入口点缺失" },
        { 0xC0000142u, "DLL 初始化失败" },
        { 0x40000015u, "CRT abort (未捕获 C++ 异常或断言失败)" },
        { 0x80000003u, "断点命中" },
        { 0x0000DEADu, "被沙箱强制终止 (超时)" },
    };
    for (const Entry& e : kMap) {
        if (e.code == code) {
            crashed = true;
            char buf[256];
            snprintf(buf, sizeof(buf), "%s [0x%08X]", e.desc, (unsigned)code);
            return buf;
        }
    }
    crashed = true;
    if (code < 0x100u) {
        if (code == 3) return "CRT abort (未捕获异常、断言失败或主动中止) [退出码 3]";
        char buf[128];
        snprintf(buf, sizeof(buf), "非零退出 [退出码 %u]", (unsigned)code);
        return buf;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "异常退出 [0x%08X]", (unsigned)code);
    return buf;
}

std::string Quote(const std::string& s) {
    return "\"" + s + "\"";
}

bool ReadWholeFile(const std::string& path, std::string& out) {
    std::vector<unsigned char> raw;
    if (!Gvtcier_File_Gopen(path.c_str(), raw)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

std::string JoinPath(const std::string& dir, const char* name) {
    if (dir.empty()) return std::string(name);
    char last = dir[dir.size() - 1];
    if (last == '\\' || last == '/') return dir + name;
    return dir + "\\" + name;
}

std::string CurrentDirC() {
    char buf[MAX_PATH * 2];
    DWORD n = GetCurrentDirectoryA(sizeof(buf), buf);
    if (n == 0 || n >= sizeof(buf)) return std::string();
    return buf;
}

void Print(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

void PrintHelp() {
    Print("用法:\n");
    Print("  sandbox -run <程序> [参数...]\n");
    Print("\n示例:\n");
    Print("  sandbox -run dist\\main.exe -game dist\\Games\\测试场景\\测试场景.dll\n");
    Print("  sandbox -run dist\\main.exe -gvts test\\test.gvts\n");
    Print("  sandbox -run dist\\Gvtcier.exe --server 22800 room1\n");
    Print("\n选项:\n");
    Print("  -run <程序>    目标程序, 相对或绝对路径\n");
    Print("  -timeout <秒>  超过时长强制终止, 缺省不限\n");
    Print("  -dir <目录>    工作目录, 缺省为程序所在目录\n");
    Print("  -quiet         不回显输出到终端, 只写日志\n");
    Print("  -shot <秒>     运行到指定秒数抓一张窗口截图 (可多次)\n");
    Print("  -shots <秒,...> 逗号分隔多个截图时刻, 如 3,8,15\n");
    Print("  -click <秒,u,v> 到点向游戏窗口客户区发送鼠标点击 (u/v 为 0~1 相对坐标, 可多次)\n");
    Print("  -key <秒,vk>   到点向游戏窗口发送虚拟键 (如 0x0D 回车, 可多次)\n");
    Print("  -h             显示本帮助\n");
    Print("\n调试接管:\n");
    Print("  -run 之后的参数原样转发给目标程序;\n");
    Print("  子进程的 stdout/stderr 全量写入会话日志,\n");
    Print("  跑完自动收割工作目录下的 crash.log(栈回溯) 与 gvtcier.log。\n");
    Print("\n产物:\n");
    Print("  logs\\session_<时间戳>.log   完整输出 + 引擎日志\n");
    Print("  logs\\crash_<时间戳>.log     崩溃报告 (崩溃或有栈回溯时)\n");
    Print("\n无参数启动则进入交互终端, 输入 help 查看命令。\n");
}

}

std::string g_lastWorkDir;
std::string g_lastLogDir;
std::string g_lastDesc;

typedef int (*GvtcierRunFn)(void*, const char*, const char*);

int Sandbox_RunEngine(int argc, char** argv);

int Sandbox_RunOnce(int argc, char** argv) {
    std::string runExe;
    std::string& workDir = g_lastWorkDir;
    workDir.clear();
    std::string childArgs;
    std::vector<std::string> childArgList;
    bool quiet = false;
    DWORD timeoutMs = 0;
    std::vector<DWORD> shotSecs;
    std::vector<std::string> clickPlan;
    std::vector<std::string> keyPlan;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { PrintHelp(); return 0; }
        else if (a == "-run" && i + 1 < argc) { runExe = argv[++i]; }
        else if (a == "-timeout" && i + 1 < argc) {
            int sec = atoi(argv[++i]);
            if (sec > 0) timeoutMs = (DWORD)sec * 1000u;
        }
        else if (a == "-dir" && i + 1 < argc) { workDir = argv[++i]; }
        else if (a == "-shot" && i + 1 < argc) {
            int sec = atoi(argv[++i]);
            if (sec >= 0) shotSecs.push_back((DWORD)sec);
        }
        else if (a == "-shots" && i + 1 < argc) {
            std::string list = argv[++i];
            size_t p = 0;
            while (p <= list.size()) {
                size_t q = list.find(',', p);
                std::string one = list.substr(p, q == std::string::npos ? std::string::npos : q - p);
                int sec = atoi(one.c_str());
                if (sec >= 0) shotSecs.push_back((DWORD)sec);
                if (q == std::string::npos) break;
                p = q + 1;
            }
        }
        else if (a == "-click" && i + 1 < argc) { clickPlan.push_back(argv[++i]); }
        else if (a == "-key" && i + 1 < argc) { keyPlan.push_back(argv[++i]); }
        else if (a == "-quiet") { quiet = true; }
        else if (!runExe.empty()) {
            childArgs += " " + Quote(a);
            childArgList.push_back(a);
        }
        else {
            Print("未知参数: %s\n\n", a.c_str());
            PrintHelp();
            return 1;
        }
    }

    if (runExe.empty()) {
        PrintHelp();
        return 1;
    }

    std::string exeAbs = FullPath(runExe);
    if (GetFileAttributesA(exeAbs.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Print("找不到可执行文件: %s\n", exeAbs.c_str());
        return 1;
    }
    if (workDir.empty()) {
        size_t sl = exeAbs.find_last_of('\\');
        workDir = (sl == std::string::npos) ? std::string(".") : exeAbs.substr(0, sl);
    }

    std::string logDir = JoinDir(workDir, "logs");
    EnsureDir(logDir);
    g_lastLogDir = logDir;

    std::string stamp = TimeStamp();
    std::string logPath = JoinDir(logDir, ("session_" + stamp + ".log").c_str());
    std::string crashPath = JoinDir(logDir, ("crash_" + stamp + ".log").c_str());

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = NULL, outWrite = NULL, errRead = NULL, errWrite = NULL;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) { Print("创建管道失败\n"); return 1; }
    if (!CreatePipe(&errRead, &errWrite, &sa, 0)) { Print("创建管道失败\n"); return 1; }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    std::string cmd = Quote(exeAbs) + childArgs;
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    Print("启动 %s\n", cmd.c_str());
    Print("日志 %s\n\n", logPath.c_str());

    BOOL ok = CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE,
                             0, NULL, workDir.c_str(), &si, &pi);
    if (!ok) {
        Print("启动失败, 错误码 %lu\n", GetLastError());
        CloseHandle(outRead); CloseHandle(outWrite);
        CloseHandle(errRead); CloseHandle(errWrite);
        return 1;
    }
    CloseHandle(outWrite);
    CloseHandle(errWrite);

    FILE* log = Gvtcier_File_Gfh(logPath.c_str(), L"wb");
    if (log) {
        char head[1024];
        snprintf(head, sizeof(head),
                 "=== Gvtcier 会话日志 ===\n"
                 "程序: %s\n"
                 "工作目录: %s\n"
                 "开始: %s\n\n",
                 cmd.c_str(), workDir.c_str(), stamp.c_str());
        fwrite(head, 1, strlen(head), log);
        fflush(log);
    }

    std::string captured;
    captured.reserve(1 << 20);
    char buf[8192];
    DWORD start = GetTickCount();
    bool timedOut = false;

    auto forward = [&](const char* data, DWORD n) {
        if (log) { fwrite(data, 1, n, log); fflush(log); }
        if (!quiet) { fwrite(data, 1, n, stdout); fflush(stdout); }
        if (captured.size() < (16u << 20)) captured.append(data, n);
    };

    auto drain = [&](HANDLE h) -> bool {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) || avail == 0) return false;
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof(buf) - 1, &got, NULL) || got == 0) return false;
        buf[got] = 0;
        forward(buf, got);
        return true;
    };

    for (;;) {
        if (drain(outRead)) continue;
        if (drain(errRead)) continue;

        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
            for (int i = 0; i < 256; ++i)
                if (!drain(outRead) && !drain(errRead)) break;
            break;
        }
        for (size_t si = 0; si < shotSecs.size(); ++si) {
            if (shotSecs[si] == 0) continue;
            if (GetTickCount() - start >= shotSecs[si] * 1000u) {
                DWORD at = shotSecs[si];
                shotSecs[si] = 0;
                char name[128];
                snprintf(name, sizeof(name), "shot_%s_%us.png", stamp.c_str(), (unsigned)at);
                std::string sp = JoinDir(logDir, name);
                bool got = CaptureWindowPng(pi.dwProcessId, sp);
                char line[256];
                snprintf(line, sizeof(line), "[shot] %us %s %s\n", (unsigned)at, got ? "截图" : "失败", sp.c_str());
                forward(line, (DWORD)strlen(line));
            }
        }
        for (size_t ci = 0; ci < clickPlan.size(); ++ci) {
            if (clickPlan[ci].empty()) continue;
            DWORD at = 0; float u = 0, v = 0;
            if (sscanf(clickPlan[ci].c_str(), "%u,%f,%f", &at, &u, &v) != 3) { clickPlan[ci].clear(); continue; }
            if (GetTickCount() - start >= at * 1000u) {
                clickPlan[ci].clear();
                HWND h = FindMainWndByPid(pi.dwProcessId);
                char line[128];
                snprintf(line, sizeof(line), "[click] %us %s\n", (unsigned)at, h ? "发送" : "窗口未找到");
                forward(line, (DWORD)strlen(line));
                if (h) SendClickToWindow(h, u, v);
            }
        }
        for (size_t ki = 0; ki < keyPlan.size(); ++ki) {
            if (keyPlan[ki].empty()) continue;
            DWORD at = 0; int vk = 0;
            if (sscanf(keyPlan[ki].c_str(), "%u,%d", &at, &vk) != 2) { keyPlan[ki].clear(); continue; }
            if (GetTickCount() - start >= at * 1000u) {
                keyPlan[ki].clear();
                HWND h = FindMainWndByPid(pi.dwProcessId);
                char line[128];
                snprintf(line, sizeof(line), "[key] %us vk=%d %s\n", (unsigned)at, vk, h ? "发送" : "窗口未找到");
                forward(line, (DWORD)strlen(line));
                if (h) SendKeyToWindow(h, (UINT)vk);
            }
        }
        if (timeoutMs > 0 && GetTickCount() - start > timeoutMs) {
            timedOut = true;
            TerminateProcess(pi.hProcess, 0x0000DEADu);
            WaitForSingleObject(pi.hProcess, 3000);
            break;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(outRead);
    CloseHandle(errRead);

    bool crashed = false;
    std::string desc = timedOut ? "超时终止" : DescribeExit(exitCode, crashed);
    double secs = (GetTickCount() - start) / 1000.0;

    std::string crashCollected;
    std::string engineLogCollected;
    {
        std::vector<std::string> dirs;
        dirs.push_back(workDir);
        for (size_t i = 1; i < childArgList.size(); ++i) {
            std::string a = childArgList[i];
            std::string low = a;
            for (size_t k = 0; k < low.size(); ++k) low[k] = (char)tolower((unsigned char)low[k]);
            if (low.size() >= 5 &&
                (low.substr(low.size() - 4) == ".dll" || low.substr(low.size() - 5) == ".gvts")) {
                std::string full = FullPath(a);
                size_t sl = full.find_last_of('\\');
                if (sl != std::string::npos) dirs.push_back(full.substr(0, sl));
            }
        }
        for (size_t d = 0; d < dirs.size(); ++d) {
            if (crashCollected.empty()) {
                std::string c1 = JoinPath(dirs[d], "crash.log");
                std::string tmp;
                if (ReadWholeFile(c1, tmp) && !tmp.empty())
                    crashCollected = "\n===== 引擎 crash.log (栈回溯) =====\n" + tmp;
            }
            if (engineLogCollected.empty()) {
                std::string l1 = JoinPath(dirs[d], "gvtcier.log");
                ReadWholeFile(l1, engineLogCollected);
            }
        }
    }

    std::string tail;
    tail += "\n";
    tail += "========================================\n";
    if (crashed || timedOut) {
        char line[512];
        tail += "  引擎异常终止\n";
        tail += "========================================\n";
        snprintf(line, sizeof(line), "退出码: 0x%08X\n", (unsigned)exitCode);
        tail += line;
        snprintf(line, sizeof(line), "原因: %s\n", desc.c_str());
        tail += line;
        snprintf(line, sizeof(line), "运行时长: %.1f 秒\n", secs);
        tail += line;
        snprintf(line, sizeof(line), "捕获输出: %d 字节\n", (int)captured.size());
        tail += line;
        tail += "\n最后输出片段:\n";
        tail += "----------------------------------------\n";
        const size_t kTail = 4096;
        if (captured.size() <= kTail) {
            tail += captured;
        } else {
            size_t cut = captured.size() - kTail;
            size_t nl = captured.find('\n', cut);
            if (nl != std::string::npos) cut = nl + 1;
            char skip[64];
            snprintf(skip, sizeof(skip), "(前部省略 %d 字节)\n", (int)cut);
            tail += skip;
            tail += captured.substr(cut);
        }
    } else {
        char line[256];
        tail += "  会话正常结束\n";
        tail += "========================================\n";
        snprintf(line, sizeof(line), "运行时长: %.1f 秒\n", secs);
        tail += line;
        snprintf(line, sizeof(line), "捕获输出: %d 字节\n", (int)captured.size());
        tail += line;
    }
    tail += "========================================\n";

    if (log) {
        fwrite(tail.c_str(), 1, tail.size(), log);
        if (!crashCollected.empty()) fwrite(crashCollected.c_str(), 1, crashCollected.size(), log);
        if (!engineLogCollected.empty()) {
            std::string h = "\n===== 引擎 gvtcier.log =====\n";
            fwrite(h.c_str(), 1, h.size(), log);
            fwrite(engineLogCollected.c_str(), 1, engineLogCollected.size(), log);
        }
        if (crashed || timedOut) {
            std::string sess = "会话日志: " + logPath + "\n";
            fwrite(sess.c_str(), 1, sess.size(), log);
        }
        fclose(log);
    }

    if (crashed || timedOut || !crashCollected.empty()) {
        FILE* cf = Gvtcier_File_Gfh(crashPath.c_str(), L"wb");
        if (cf) {
            char head[1024];
            snprintf(head, sizeof(head),
                     "=== Gvtcier 崩溃报告 ===\n"
                     "程序: %s\n"
                     "工作目录: %s\n"
                     "完整日志: %s\n",
                     cmd.c_str(), workDir.c_str(), logPath.c_str());
            fwrite(head, 1, strlen(head), cf);
            fwrite(tail.c_str(), 1, tail.size(), cf);
            if (!crashCollected.empty())
                fwrite(crashCollected.c_str(), 1, crashCollected.size(), cf);
            if (!engineLogCollected.empty()) {
                std::string h = "\n===== 引擎 gvtcier.log =====\n";
                fwrite(h.c_str(), 1, h.size(), cf);
                fwrite(engineLogCollected.c_str(), 1, engineLogCollected.size(), cf);
            }
            fflush(cf);
            fclose(cf);
        }
    }

    Print("%s", tail.c_str());
    g_lastDesc = desc;
    if (timedOut) g_lastDesc = "超时终止";
    if (crashed || timedOut) {
        Print("崩溃报告 %s\n", crashPath.c_str());
        Print("完整日志 %s\n", logPath.c_str());
        return 1;
    }
    Print("完整日志 %s\n", logPath.c_str());
    return 0;
}

int Sandbox_RunEngine(int argc, char** argv) {
    std::string target;
    std::string engineDll;
    std::string gameArgs;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-start" && i + 1 < argc) { target = argv[++i]; }
        else if (a == "-engine" && i + 1 < argc) { engineDll = argv[++i]; }
        else if (a == "-args" && i + 1 < argc) { gameArgs = argv[++i]; }
        else if (a == "-h" || a == "--help") {
            Print("用法:\n");
            Print("  sandbox -start <游戏名.dll 或 脚本.gvts> [-engine <Gvtcier.dll>] [-args <参数>]\n");
            Print("\n说明:\n");
            Print("  沙箱直接加载引擎 Gvtcier.dll 并调用其 Gvtcier_Run 入口,\n");
            Print("  不经过 main.exe。引擎的全部 printf 与栈回溯均被接管并落盘。\n");
            Print("\n示例:\n");
            Print("  sandbox -start dist\\Games\\测试场景\\测试场景.dll\n");
            Print("  sandbox -start test\\test.gvts -engine dist\\Gvtcier.dll\n");
            Print("\n产物:\n");
            Print("  logs\\session_<时间戳>.log   引擎全部输出\n");
            Print("  logs\\crash_<时间戳>.log     崩溃报告 (含栈回溯)\n");
            return 0;
        }
    }

    if (target.empty()) {
        Print("缺少 -start 目标。用法: sandbox -start <游戏名.dll 或 脚本.gvts>\n");
        return 1;
    }

    std::string targetAbs = FullPath(target);
    if (GetFileAttributesA(targetAbs.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Print("找不到目标: %s\n", targetAbs.c_str());
        return 1;
    }

    std::string targetDir;
    {
        size_t sl = targetAbs.find_last_of('\\');
        targetDir = (sl == std::string::npos) ? std::string(".") : targetAbs.substr(0, sl);
    }
    if (engineDll.empty()) engineDll = targetDir + "\\Gvtcier.dll";
    std::string engineAbs = FullPath(engineDll);
    if (GetFileAttributesA(engineAbs.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::string alt = targetDir + "\\..\\Gvtcier.dll";
        if (GetFileAttributesA(FullPath(alt).c_str()) != INVALID_FILE_ATTRIBUTES)
            engineAbs = FullPath(alt);
        else {
            Print("找不到引擎 DLL: %s\n", engineAbs.c_str());
            Print("可用 -engine <Gvtcier.dll 路径> 指定。\n");
            return 1;
        }
    }

    std::string logDir = JoinDir(targetDir, "logs");
    EnsureDir(logDir);
    g_lastLogDir = logDir;
    std::string stamp = TimeStamp();
    std::string logPath = JoinDir(logDir, ("session_" + stamp + ".log").c_str());
    std::string crashPath = JoinDir(logDir, ("crash_" + stamp + ".log").c_str());

    Print("沙箱直调引擎\n");
    Print("  目标: %s\n", targetAbs.c_str());
    Print("  引擎: %s\n", engineAbs.c_str());
    Print("  日志: %s\n\n", logPath.c_str());

    std::string oldCwd = CurrentDirC();
    if (!targetDir.empty()) SetCurrentDirectoryA(targetDir.c_str());

    FILE* cat = freopen(logPath.c_str(), "wb", stdout);
    if (!cat) Print("(无法重定向输出到日志, 仅在终端显示)\n");
    std::string errPathHold;
    std::string engLog = JoinDir(targetDir, "gvtcier.log");
    std::string crashFile = JoinDir(targetDir, "crash.log");
    DeleteFileA(crashFile.c_str());

    HMODULE engine = LoadLibraryA(engineAbs.c_str());
    int rc = 1;
    if (!engine) {
        Print("加载引擎失败, 错误码 %lu\n", GetLastError());
    } else {
        auto runFn = (GvtcierRunFn)GetProcAddress(engine, "Gvtcier_Run");
        if (!runFn) {
            Print("引擎缺少 Gvtcier_Run 导出\n");
        } else {
            rc = runFn(engine, targetAbs.c_str(), gameArgs.empty() ? nullptr : gameArgs.c_str());
            Print("\n[engine returned %d]\n", rc);
        }
        FreeLibrary(engine);
    }
    fflush(stdout);
    if (cat) fclose(cat);
    if (!oldCwd.empty()) SetCurrentDirectoryA(oldCwd.c_str());

    FILE* sess = Gvtcier_File_Gfh(logPath.c_str(), L"ab");
    if (sess) {
        std::string eng;
        if (ReadWholeFile(engLog, eng) && !eng.empty()) {
            std::string h = "\n===== 引擎 gvtcier.log =====\n";
            fwrite(h.c_str(), 1, h.size(), sess);
            fwrite(eng.c_str(), 1, eng.size(), sess);
        }
        std::string cr;
        if (ReadWholeFile(crashFile, cr) && !cr.empty()) {
            std::string h = "\n===== 引擎 crash.log (栈回溯) =====\n";
            fwrite(h.c_str(), 1, h.size(), sess);
            fwrite(cr.c_str(), 1, cr.size(), sess);
        }
        fclose(sess);
    }

    std::string cr;
    if (ReadWholeFile(crashFile, cr) && !cr.empty()) {
        FILE* cf = Gvtcier_File_Gfh(crashPath.c_str(), L"wb");
        if (cf) {
            std::string h = "=== Gvtcier 崩溃报告 (含栈回溯) ===\n";
            fwrite(h.c_str(), 1, h.size(), cf);
            fwrite(cr.c_str(), 1, cr.size(), cf);
            fclose(cf);
        }
    }

    Print("引擎返回 %d\n", rc);
    Print("完整日志 %s\n", logPath.c_str());
    if (GetFileAttributesA(crashPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        Print("崩溃报告 %s\n", crashPath.c_str());
    g_lastDesc = (rc == 0) ? std::string("正常结束") : std::string("引擎返回非零");
    return rc;
}

namespace {

std::vector<std::string> SplitArgs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote && (c == ' ' || c == '\t')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string CurrentDir() {
    char buf[MAX_PATH * 2];
    DWORD n = GetCurrentDirectoryA(sizeof(buf), buf);
    if (n == 0 || n >= sizeof(buf)) return "?";
    return buf;
}

void SetDir(const std::string& d) {
    if (!SetCurrentDirectoryA(d.c_str()))
        Print("无法进入目录: %s (错误码 %lu)\n", d.c_str(), GetLastError());
}

void ListDir() {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*", &fd);
    if (h == INVALID_HANDLE_VALUE) { Print("无法列出目录\n"); return; }
    int dirs = 0, files = 0;
    do {
        if (strcmp(fd.cFileName, ".") == 0) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir) {
            if (strcmp(fd.cFileName, "..") == 0) { Print("  <上级>\n"); continue; }
            Print("  [目录] %s\n", fd.cFileName);
            dirs++;
        } else {
            Print("  %-40s %8.1f KB\n", fd.cFileName, fd.nFileSizeLow / 1024.0);
            files++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    Print("  共 %d 个目录, %d 个文件\n", dirs, files);
}

int RunInteractive(int argc, char** argv);

}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc > 1) {
        for (int i = 1; i < argc; ++i)
            if (strcmp(argv[i], "-start") == 0) return Sandbox_RunEngine(argc, argv);
        return Sandbox_RunOnce(argc, argv);
    }
    return RunInteractive(argc, argv);
}

namespace {

int RunInteractive(int argc, char** argv) {
    Print("Gvtcier 沙箱  输入 help 查看命令, exit 退出\n");
    Print("----------------------------------------\n");

    std::string lastResult;
    std::string lastRunDir;
    for (;;) {
        std::string cwd = CurrentDir();
        Print("\n%s", cwd.c_str());
        if (!lastResult.empty()) Print("  [上次: %s]", lastResult.c_str());
        if (!lastRunDir.empty() && lastRunDir != cwd)
            Print("  [日志: %s]", lastRunDir.c_str());
        Print("\n> ");

        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::vector<std::string> a = SplitArgs(line);
        if (a.empty()) continue;
        std::string cmd = a[0];

        if (cmd == "exit" || cmd == "quit") break;

        if (cmd == "help") {
            Print("\n可用命令:\n");
            Print("  run <exe> [参数...]   运行程序, 输出写入日志\n");
            Print("  cd <目录>             切换工作目录\n");
            Print("  ls                    列出当前目录\n");
            Print("  pwd                   显示当前目录\n");
            Print("  crash                 显示最近的崩溃报告\n");
            Print("  logs                  打开日志目录\n");
            Print("  help                  显示本帮助\n");
            Print("  exit | quit           退出\n");
            Print("\nrun 的附加选项:\n");
            Print("  -timeout <秒>         超时强制终止\n");
            Print("  -quiet                不回显, 只写日志\n");
            Print("  -dir <目录>           指定子进程工作目录\n");
            Print("\n示例:\n");
            Print("  run ..\\dist\\Gvtcier.exe\n");
            Print("  run ..\\dist\\Gvtcier.exe game.gvts\n");
            Print("  run ..\\dist\\Gvtcier.exe --server 22800 room1 -timeout 60\n");
            continue;
        }

        if (cmd == "pwd") { Print("%s\n", CurrentDir().c_str()); continue; }

        if (cmd == "cd") {
            if (a.size() < 2) { Print("%s\n", CurrentDir().c_str()); continue; }
            SetDir(a[1]);
            continue;
        }

        if (cmd == "ls") { ListDir(); continue; }

        if (cmd == "logs") {
            std::string d = g_lastLogDir.empty() ? (CurrentDir() + "\\logs") : g_lastLogDir;
            if (GetFileAttributesA(d.c_str()) == INVALID_FILE_ATTRIBUTES) {
                Print("暂无日志目录, 请先运行一次\n");
            } else {
                ShellExecuteA(NULL, "open", d.c_str(), NULL, NULL, SW_SHOWNORMAL);
                Print("已打开日志目录 %s\n", d.c_str());
            }
            continue;
        }

        if (cmd == "crash") {
            std::string d = g_lastLogDir.empty() ? (CurrentDir() + "\\logs") : g_lastLogDir;
            std::string pat = d + "\\crash_*.log";
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pat.c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) {
                Print("暂无崩溃报告, 仅异常终止时生成\n");
                continue;
            }
            std::string newest = fd.cFileName;
            while (FindNextFileA(h, &fd)) {
                if (strcmp(fd.cFileName, newest.c_str()) > 0) newest = fd.cFileName;
            }
            FindClose(h);
            std::string full = d + "\\" + newest;
            FILE* f = Gvtcier_File_Gfh(full.c_str(), L"rb");
            if (!f) { Print("无法打开 %s\n", full.c_str()); continue; }
            Print("\n---------- %s ----------\n", newest.c_str());
            char buf[4096];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
                fwrite(buf, 1, got, stdout);
            fclose(f);
            Print("\n---------- 报告结束 ----------\n");
            continue;
        }

        if (cmd == "run") {
            std::vector<char*> cargv;
            static std::string exeName = "sandbox";
            static std::string runFlag = "-run";
            cargv.push_back((char*)exeName.c_str());
            cargv.push_back((char*)runFlag.c_str());
            for (size_t i = 1; i < a.size(); ++i)
                cargv.push_back((char*)a[i].c_str());
            int rc = Sandbox_RunOnce((int)cargv.size(), cargv.data());
            lastResult = (rc == 0) ? std::string("正常结束") : g_lastDesc;
            lastRunDir = g_lastWorkDir;
            continue;
        }

        Print("未知命令: %s  (输入 help 查看)\n", cmd.c_str());
    }

    return 0;
}

}
