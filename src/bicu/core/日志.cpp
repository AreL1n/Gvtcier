#include "Gvtcier.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <unordered_set>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace rlog {

//旧标准输出句柄。没接日志转发时它是原样，接上之后 printf 全部改走它，等于全丢。
static FILE* g_oldStdout = nullptr;
//日志文件句柄；为空表示尚未打开或打开失败，此时所有写文件操作都静默跳过。
static FILE* g_logFile = nullptr;

//把 stdout/stderr 里的内容原样搬进日志文件。
//GUI 子系统启动的进程压根没有标准输出，printf 写出去等于扔进黑洞；
//这里换成一个临时文件当接力棒，每来一段就追加到日志里，行尾处理由日志自己负责。
static int forwardStream(FILE* f, bool toErr) {
    fflush(g_oldStdout);
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    if (n == 0) return 0;
    buf[n] = '\0';
    //日志文件还没打开时没有去处，直接算消费掉。
    if (g_logFile) {
        fwrite(buf, 1, n, g_logFile);
        fflush(g_logFile);
    }
    if (toErr) OutputDebugStringA(buf);
    return (int)n;
}

//检查 stdout 是否还挂着一个打开的句柄。开发机上的宿主程序本身是控制台程序，
//会带着控制台和句柄进来，这种场合什么都别动，不然控制台里就看不到东西了。
static bool stdoutHasHandle() {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    DWORD type = GetFileType(h);
    return type != FILE_TYPE_UNKNOWN;
#else
    return true;
#endif
}

//让 printf（以及任何直接用 stdout 的代码）的结果落到日志文件里。
//必须在日志文件打开之后调用，否则没有落地的地方。
void Gvtcier_Log_ForwardStdout() {
#if defined(_WIN32)
    if (g_oldStdout) return;
    if (stdoutHasHandle()) return;
    //临时文件当缓冲池：进程退出或崩溃时残留几百字节无所谓，日志本身是另一份。
    char tmpPath[MAX_PATH] = {};
    DWORD n = GetTempPathA(MAX_PATH, tmpPath);
    if (n == 0) return;
    std::string tmp = std::string(tmpPath) + "gvtcier_stdout.tmp";
    FILE* f = fopen(tmp.c_str(), "w+");
    if (!f) {
        //兜底：临时目录写不进去就退回系统空设备，至少 printf 不会写失败。
        f = fopen("NUL", "w");
        if (!f) return;
    }
    g_oldStdout = stdout;
    _dup2(_fileno(f), _fileno(stdout));
    _dup2(_fileno(f), _fileno(stderr));
    setvbuf(f, nullptr, _IONBF, 0);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif
}

//把标准输出交还给系统。崩溃报告要弹框、要调外部工具，这时候别让它们往日志里糊。
void Gvtcier_Log_StopForward() {
#if defined(_WIN32)
    if (!g_oldStdout) return;
    fflush(stdout);
    fflush(stderr);
    int nullFd = _open("NUL", _O_WRONLY);
    if (nullFd >= 0) {
        _dup2(nullFd, _fileno(stdout));
        _dup2(nullFd, _fileno(stderr));
        _close(nullFd);
    }
    int saved = _dup(_fileno(g_oldStdout));
    g_oldStdout = nullptr;
    if (saved >= 0) _close(saved);
#endif
}

//已打印过的去重键集合。用于 rlog::once，避免同一类错误在每帧重复刷屏。
static std::unordered_set<std::string>& seen() {
    static std::unordered_set<std::string> s;
    return s;
}

//实际打开的日志路径，用于打印与排查。
static char g_logPath[1024] = {};

//打开日志文件。只执行一次，重复调用直接返回。
//pathHint 为期望路径；传空则用当前目录下的 gvtcier.log。
//若期望路径打不开（目录不存在、无权限），退回到当前目录的文件名，尽量保证还有日志可查。
//副作用：打开后关闭缓冲并立即写入一行开头标记，确保日志文件一创建就能看出是本次运行产生的。
void Gvtcier_Log_InitFile(const char* pathHint) {
    if (g_logFile) return;

    char path[1024] = {};
    if (pathHint && pathHint[0]) {
        snprintf(path, sizeof(path), "%s", pathHint);
    } else {
        snprintf(path, sizeof(path), "gvtcier.log");
    }

    g_logFile = Gvtcier_File_Gfh(path, L"w");
    if (!g_logFile) {
        snprintf(path, sizeof(path), "gvtcier.log");
        g_logFile = Gvtcier_File_Gfh(path, L"w");
    }
    if (g_logFile) {
        snprintf(g_logPath, sizeof(g_logPath), "%s", path);
        //无缓冲：崩溃时进程可能来不及 flush，关闭缓冲能保住最后几行。
        setvbuf(g_logFile, nullptr, _IONBF, 0);
        fprintf(g_logFile, "=== Gvtcier log opened ===\n");
        fflush(g_logFile);
    }
    printf("[log] file = %s\n", g_logFile ? g_logPath : "(none)");
    fflush(stdout);
    //日志准备好之后才把 printf 接过来，否则这一行自己也接不进去。
    Gvtcier_Log_ForwardStdout();
}

//按需从标准输出搬内容进日志。所有日志入口都会调一下，谁先写谁负责搬运。
void Gvtcier_Log_Pump() {
    if (stdout && g_oldStdout) forwardStream(stdout, false);
}

void Gvtcier_Log_PumpErr() {
    if (stderr && g_oldStdout) forwardStream(stderr, true);
}

//关闭日志文件并写一行结束标记。正常退出路径调用；异常退出时不会有这一行。
void Gvtcier_Log_Close() {
    //收尾前先把积压的输出搬干净，否则最后几行 printf 会留在临时文件里。
    Gvtcier_Log_Pump();
    Gvtcier_Log_PumpErr();
    if (g_logFile) {
        fprintf(g_logFile, "=== Gvtcier log closed ===\n");
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

//只写日志文件，不输出到控制台。用于高频或不需要打扰玩家的记录。
//每次写完立即 flush，保证异常退出时最后一条也在文件里。日志未打开时什么都不做。
void Gvtcier_Log_Write(const char* fmt, ...) {
    if (!g_logFile) return;
    Gvtcier_Log_Pump();
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fflush(g_logFile);
}

//同时输出到控制台与日志文件。用于玩家/开发者需要当场看到的节点性事件（如状态切换）。
//日志未打开时仍会输出到控制台。
void Gvtcier_Log_Echo(const char* fmt, ...) {
    Gvtcier_Log_Pump();
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (g_logFile) {
        va_start(ap, fmt);
        vfprintf(g_logFile, fmt, ap);
        va_end(ap);
        fflush(g_logFile);
    }
}

//按 key 去重的一次性输出：同一个 key 只打印第一次，后续调用直接丢弃。
//key 一般取"类别:具体内容"（如着色器编译错误日志），这样不同错误各打一次，同一错误不刷屏。
void once(const char* key, const char* fmt, ...) {
    if (seen().count(key)) return;
    seen().insert(key);
    Gvtcier_Log_Pump();
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (g_logFile) {
        va_start(ap, fmt);
        vfprintf(g_logFile, fmt, ap);
        va_end(ap);
        fflush(g_logFile);
    }
}

//清除某个去重键，使同一 key 之后可以再打印一次。用于"问题已修复后又复现"的场景。
void reset(const char* key) { seen().erase(key); }

// 统一的「一行结果」输出。成功只有一行，失败多带原因。
// 格式刻意对齐：prefix 不同但列宽一致，日志竖着扫能一眼看出谁失败了。
static void resultLine(const char* prefix, const char* name, bool ok, const char* detail) {
    if (ok) {
        Gvtcier_Log_Write(u8"%s %s 成功\n", prefix, name);
    } else {
        Gvtcier_Log_Write(u8"%s %s 失败: %s\n", prefix, name, (detail && detail[0]) ? detail : "未知原因");
    }
}

void Gvtcier_Log_Check(const char* name, bool ok, const char* detail) {
    resultLine(u8"[自检]", name, ok, detail);
}

void Gvtcier_Log_Asset(const char* name, bool ok, const char* detail) {
    resultLine(u8"[资源]", name, ok, detail);
}

void Gvtcier_Log_Banner(const char* name, bool ok, const char* version) {
    if (ok) {
        if (version && version[0]) Gvtcier_Log_Write(u8"[%s] 初始化成功 %s\n", name, version);
        else Gvtcier_Log_Write(u8"[%s] 初始化成功\n", name);
    } else {
        Gvtcier_Log_Write(u8"[%s] 初始化失败\n", name);
    }
}

void Gvtcier_Log_BannerLine(const char* key, const char* value) {
    Gvtcier_Log_Write(u8"|____%s: %s\n", key, (value && value[0]) ? value : "(未知)");
}

void Gvtcier_Log_Stage(int index, const char* title) {
    Gvtcier_Log_Write(u8"\n===== 阶段 %d: %s =====\n", index, title);
}

}
