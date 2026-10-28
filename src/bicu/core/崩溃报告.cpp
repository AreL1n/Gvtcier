#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <windows.h>
#include <dbghelp.h>

//崩溃信息统一出口：同时写控制台与 crash.log。
//之所以每个片段单独追加并立即返回，是为了在崩溃现场尽量少做分配、少依赖堆的状态，
//避免写日志本身再次崩溃导致什么都留不下。
static void writeCrashLog(const char* msg) {
    printf("%s", msg);
    fflush(stdout);
    //printf 已经被日志接管，这时候必须搬一次，崩溃现场的输出才不会丢。
    rlog::Gvtcier_Log_Pump();
    Gvtcier_File_Gappend("crash.log", msg, strlen(msg));
}

//用 MinGW 自带的 addr2line 把一组地址翻译成"文件:行号"。
//之所以在崩溃时再调外部工具，是因为发布版通常不带调试符号索引，MS 的符号接口拿不到行号，
//而 addr2line 可以直接读可执行文件里的调试信息。
//工具路径按发布包的相对布局推算；找不到工具就静默跳过，不影响已有调用栈的输出。
//addrs 为待翻译地址数组，count 为个数。
static void writeAddr2Line(const DWORD64* addrs, int count) {
    std::string exePath = Gvtcier_W32_ModulePathUtf8();
    if (exePath.empty()) return;
    std::string exeDir = Gvtcier_W32_ModuleDirUtf8();
    std::string tool = exeDir + "\\..\\tools\\Mingw\\bin\\addr2line.exe";
    if (!Gvtcier_W32_FileExistsUtf8(tool.c_str())) return;
    std::string cmd = "\"" + tool + "\" -e \"" + exePath + "\" -f -C -a";
    for (int i = 0; i < count; ++i) {
        char tmp[64];
        _snprintf(tmp, sizeof(tmp), " 0x%llX", (unsigned long long)addrs[i]);
        cmd += tmp;
    }
    //-f 输出函数名，-C 还原 C++ 修饰名，-a 每段前打印地址，便于与调用栈对齐。
    FILE* p = _popen(cmd.c_str(), "r");
    if (!p) return;
    char line[512];
    //输出缓冲固定 4KB：崩溃路径上不做动态扩容，超长输出直接截断。
    char out[4096];
    size_t used = 0;
    out[0] = 0;
    while (fgets(line, sizeof(line), p)) {
        size_t l = strlen(line);
        if (used + l >= sizeof(out) - 2) break;
        memcpy(out + used, line, l);
        used += l;
    }
    _pclose(p);
    out[used] = 0;
    if (out[0]) {
        char hdr[64];
        _snprintf(hdr, sizeof(hdr), "\naddr2line:\n");
        writeCrashLog(hdr);
        writeCrashLog(out);
    }
}

//未处理异常过滤器：任何崩溃都会进到这里，先记录异常码与出错地址，再走一遍调用栈。
//返回 EXCEPTION_EXECUTE_HANDLER 表示已处理，进程随即结束（崩溃现场已不可信，不适合继续运行）。
//ep 由系统提供，含异常记录与线程上下文；为空时交回系统默认处理。
static LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    if (!ep) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    ULONG_PTR addr = (ULONG_PTR)ep->ExceptionRecord->ExceptionAddress;
    char msg[1024];
    _snprintf(msg, sizeof(msg), "\n=== Gvtcier Crash ===\nCode: 0x%08X  Addr: 0x%p\n", (unsigned)code, (void*)addr);
    writeCrashLog(msg);

    //初始化符号处理：UNDNAME 还原 C++ 名，DEFERRED_LOADS 延迟加载符号表以免崩溃时长时间卡住，
    //LOAD_LINES 允许查行号。FALSE 表示不加载全部模块的符号，只按需加载。
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);

    //SYMBOL_INFO 是变长结构，尾部自带名字缓冲区，这里在栈上留出 512 字节给函数名，避免崩溃时申请堆内存。
    char symbolBuf[sizeof(SYMBOL_INFO) + 512];
    PSYMBOL_INFO si = (PSYMBOL_INFO)symbolBuf;
    IMAGEHLP_LINE64 lineInfo;
    DWORD lineDisp = 0;
    IMAGEHLP_MODULE64 modInfo;
    //写单帧信息：优先输出"模块!函数+偏移 (文件:行号)"，缺哪一部分就退到次一级格式，
    //保证即使符号不全也能给出模块名加偏移，至少能判断崩在哪个 DLL。idx 为负表示异常地址本身而非栈帧序号。
    auto writeFrame = [&](DWORD64 frameAddr, int idx) {
        char where[600] = "";
        memset(symbolBuf, 0, sizeof(symbolBuf));
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 511;
        DWORD64 disp2 = 0;
        bool haveSym = SymFromAddr(GetCurrentProcess(), frameAddr, &disp2, si) != FALSE;
        memset(&modInfo, 0, sizeof(modInfo));
        modInfo.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        bool haveMod = SymGetModuleInfo64(GetCurrentProcess(), frameAddr, &modInfo) != FALSE;
        memset(&lineInfo, 0, sizeof(lineInfo));
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        bool haveLine = SymGetLineFromAddr64(GetCurrentProcess(), frameAddr, &lineDisp, &lineInfo) != FALSE;
        const char* mod = haveMod ? modInfo.ModuleName : "?";
        if (haveSym) {
            char fn[512];
            _snprintf(fn, sizeof(fn), "%s+0x%llX", si->Name, (unsigned long long)disp2);
            if (haveLine)
                _snprintf(where, sizeof(where), "%s!%s (%s:%lu)", mod, fn, lineInfo.FileName, (unsigned long)lineInfo.LineNumber);
            else
                _snprintf(where, sizeof(where), "%s!%s", mod, fn);
        } else if (haveLine) {
            _snprintf(where, sizeof(where), "%s!?(%s:%lu)", mod, lineInfo.FileName, (unsigned long)lineInfo.LineNumber);
        } else if (haveMod) {
            DWORD64 base = modInfo.BaseOfImage;
            _snprintf(where, sizeof(where), "%s+0x%llX", mod, (unsigned long long)(frameAddr - base));
        }
        if (idx < 0)
            _snprintf(msg, sizeof(msg), "  at 0x%llX  %s\n", (unsigned long long)frameAddr, where[0] ? where : "(unknown)");
        else
            _snprintf(msg, sizeof(msg), "  #%d  0x%llX  %s\n", idx, (unsigned long long)frameAddr, where[0] ? where : "(unknown)");
        writeCrashLog(msg);
    };

    writeFrame((DWORD64)addr, -1);

    //最多记录 64 帧：栈太深通常是递归或栈损坏，继续走意义不大，长度上限也保证崩溃路径耗时有界。
    DWORD64 frameAddrs[64];
    int frameCount = 0;
    _snprintf(msg, sizeof(msg), "\nStack trace:\n");
    writeCrashLog(msg);

    //手工填好栈帧起始状态，再从线程上下文一步步回溯。AddrModeFlat 表示地址是平坦的绝对地址，不做段选择。
    STACKFRAME64 sf;
    memset(&sf, 0, sizeof(sf));
    CONTEXT ctx = *ep->ContextRecord;
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Mode = AddrModeFlat;

    //IMAGE_FILE_MACHINE_AMD64 写死 64 位 x64：本程序的崩溃回溯只支持这一个架构。
    for (int i = 0; i < 64 && frameCount < 64; ++i) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(),
                         &sf, &ctx, NULL, &SymFunctionTableAccess64, &SymGetModuleBase64, NULL))
            break;
        DWORD64 frameAddr = sf.AddrPC.Offset;
        if (frameAddr == 0) break;
        frameAddrs[frameCount++] = frameAddr;
        writeFrame(frameAddr, frameCount - 1);
    }
    writeCrashLog("========================\n");
    writeAddr2Line(frameAddrs, frameCount);

    SymCleanup(GetCurrentProcess());
    return EXCEPTION_EXECUTE_HANDLER;
}

//安装崩溃处理器。必须在启动早期调用，且应在可能崩溃的子系统初始化之前。
//注意：它只接管未被其它代码处理的异常，若别处已注册过处理器会被覆盖。
void Gvtcier_CrashReport_Install() {
    SetUnhandledExceptionFilter(crashHandler);
}
