#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include "Gvtcier.h"

//宿主进程（main.exe）的入口。它本身不含引擎逻辑，只负责找到 Gvtcier.dll、把命令行翻译成一次调用,能跑起来真是一个奇迹。
//这样拆分让引擎可以整体替换（热更新或换后端），而启动器保持极小且稳定。

//引擎入口函数指针类型：参数依次为宿主模块、游戏路径、透传的游戏参数串，返回进程退出码。
typedef int (*GvtcierRunFn)(void*, const char*, const char*);

//列出目录下所有子目录的名字（只取一层，UTF-8）。
//必须走宽字符 API：游戏目录名常是中文，ANSI 版在非 936 代码页下会读成乱码。
static std::vector<std::string> listSubDirs(const std::string& dir) {
    std::vector<std::string> out;
    std::wstring pattern = Gvtcier_W32_ToWide(dir) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        out.push_back(Gvtcier_W32_ToUtf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

//在 exe 所在目录里找游戏数据目录。
//打包出来的目录里有两个 _Data：Gvtcier_Data 放引擎运行时（固定名），
//GameName_Data 放游戏内容。这里要找的是后者，所以必须把前者排除掉。
//找不到同名的就退回目录下唯一剩下的那个 _Data，玩家改 exe 名也还能启动。
static std::string findGameData(const std::string& hostDir) {
    std::vector<std::string> subs = listSubDirs(hostDir);
    std::string match;
    int dataCount = 0;
    for (size_t i = 0; i < subs.size(); ++i) {
        const std::string& n = subs[i];
        if (n.size() < 6 || n.compare(n.size() - 5, 5, "_Data") != 0) continue;
        if (_stricmp(n.c_str(), "Gvtcier_Data") == 0) continue;
        ++dataCount;
        match = hostDir + "\\" + n;
    }
    if (dataCount == 1) return match;

    //多个游戏数据目录时按 exe 名精确匹配
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exe = exePath;
    size_t sl = exe.find_last_of("\\/");
    if (sl != std::string::npos) exe = exe.substr(sl + 1);
    size_t dot = exe.find_last_of('.');
    if (dot != std::string::npos) exe = exe.substr(0, dot);
    std::string want = exe + "_Data";
    for (size_t i = 0; i < subs.size(); ++i) {
        if (_stricmp(subs[i].c_str(), want.c_str()) == 0) return hostDir + "\\" + subs[i];
    }
    return "";
}

//在数据目录里找游戏包。优先用打包时写下的 game.txt，其次取唯一的 .gpk。
static std::string findGpkIn(const std::string& dataDir) {
    //game.txt 里只存文件名，同样要按 UTF-8 走宽字符读，中文名才不会断
    std::wstring marker = Gvtcier_W32_ToWide(dataDir + "\\game.txt");
    FILE* f = _wfopen(marker.c_str(), L"rb");
    if (f) {
        char buf[1024] = {};
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        std::string name(buf, n);
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
        if (!name.empty()) {
            std::string full = dataDir + "\\" + name;
            if (Gvtcier_W32_FileExistsUtf8(full.c_str())) return full;
        }
    }
    std::wstring pattern = Gvtcier_W32_ToWide(dataDir) + L"\\*.gpk";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string found = dataDir + "\\" + Gvtcier_W32_ToUtf8(fd.cFileName);
    bool more = FindNextFileW(h, &fd) != 0;
    FindClose(h);
    return more ? "" : found;
}

//弹出错误框，fmt 与 printf 一致。启动阶段还没有控制台与日志，错误框是唯一能让玩家看到原因的途径。
static void errBox(const char* title, const char* fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    Gvtcier_W32_MessageBoxUtf8(msg, title, MB_ICONERROR | MB_OK);
}

//输出一行启动信息：同时写控制台与调试器输出窗口。
//调试器输出窗口让用 Visual Studio 或 DbgView 的开发者也能看到启动器日志，不依赖控制台是否可attach。
static void outLine(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    printf("%s", msg);
    fflush(stdout);
    OutputDebugStringW(Gvtcier_W32_ToWide(msg).c_str());
}

//把"加载 DLL 失败"翻译成玩家能行动的说明，而不是只报一个错误码。
//判断方式是：以数据文件方式试着打开该 DLL。
//  能作为数据打开但加载失败 → 文件完好，问题在于不能当代码执行（位数不对、被安全软件拦截、被标记为"已阻止"）；
//  连数据都打不开 → 文件其实不可用，最常见原因是它依赖的运行库缺失，错误码 126 即"找不到模块"。
//  文件不存在 → 提示应与 main.exe 放在同一目录。
//dllPath 为实际尝试的路径，dllName 为展示用的文件名，loadErr 为原始系统错误码。
static void describeLoadFailure(const char* dllPath, const char* dllName, DWORD loadErr) {
    BOOL fileThere = Gvtcier_W32_FileExistsUtf8(dllPath);
    if (fileThere) {
        void* probe = Gvtcier_W32_LoadLibraryExUtf8(dllPath, LOAD_LIBRARY_AS_DATAFILE);
        DWORD probeErr = probe ? 0 : GetLastError();
        if (probe) FreeLibrary((HMODULE)probe);
        if (probe && probeErr == 0) {
            errBox("Gvtcier",
                   "%s 存在，但无法作为代码加载。\n\n"
                   "路径：%s\n"
                   "系统错误码：%lu\n\n"
                   "可能原因：\n"
                   "1. 位数不匹配（本程序为 64 位，DLL 须同为 64 位）\n"
                   "2. 文件被安全软件拦截或已损坏\n"
                   "3. 文件被标记为「已阻止」（右键属性 → 解除锁定）",
                   dllName, dllPath, (unsigned long)loadErr);
            return;
        }
        errBox("Gvtcier",
               "%s 存在，但它依赖的运行库缺失，导致加载失败。\n\n"
               "路径：%s\n"
               "系统错误码：%lu\n\n"
               "这是最常见的情况：DLL 自身缺少依赖项。\n"
               "错误码 126（ERROR_MOD_NOT_FOUND）即表示某个依赖 DLL 找不到。\n\n"
               "请确认发布包完整，或联系开发者。",
               dllName, dllPath, (unsigned long)loadErr);
        return;
    }
    errBox("Gvtcier",
           "找不到 %s。\n\n"
           "期望路径：%s\n"
           "系统错误码：%lu\n\n"
           "请确认 %s 与 main.exe 位于同一目录。",
           dllName, dllPath, (unsigned long)loadErr, dllName);
}

//用 wmain 而非 main：命令行参数天然是宽字符，中文路径无需先猜代码页。
int wmain(int argc, wchar_t** argv) {
    std::string hostDir = Gvtcier_W32_ModuleDirUtf8();
    if (hostDir.empty()) {
        errBox("Gvtcier", "无法获取 main.exe 所在目录（错误码 %lu）。", (unsigned long)GetLastError());
        return 1;
    }
    outLine("[host] exe dir = %s\n", hostDir.c_str());

    std::string enginePath = hostDir + "\\Gvtcier.dll";
    outLine("[host] engine path = %s\n", enginePath.c_str());

    //先清空错误码再加载，这样失败时能确认错误确实来自这次调用而不是之前的残留。
    SetLastError(0);
    HMODULE engine = (HMODULE)Gvtcier_W32_LoadLibraryUtf8(enginePath.c_str());
    if (!engine) {
        DWORD e = GetLastError();
        outLine("[host] LoadLibrary failed, err=%lu\n", (unsigned long)e);
        describeLoadFailure(enginePath.c_str(), "Gvtcier.dll", e);
        return 1;
    }

    auto runFn = (GvtcierRunFn)GetProcAddress(engine, "Gvtcier_Run");
    if (!runFn) {
        DWORD e = GetLastError();
        errBox("Gvtcier",
               "Gvtcier.dll 已加载，但缺少 Gvtcier_Run 导出。\n\n"
               "路径：%s\n"
               "系统错误码：%lu\n\n"
               "可能原因：\n"
               "1. Gvtcier.dll 与 main.exe 版本不匹配\n"
               "2. 导出名被变更（当前期望 Gvtcier_Run）",
               enginePath.c_str(), (unsigned long)e);
        FreeLibrary(engine);
        return 1;
    }

    //把宽字符参数转成 UTF-8，后续按普通字符串处理。
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(Gvtcier_W32_ToUtf8(argv[i]));

    //命令行解析：支持显式开关与"直接拖入文件"两种用法。
    //gamePath 游戏包/DLL，gvtsPath 裸脚本，checkPath 仅校验语法，gameArgs 原样透传给游戏。
    std::string gamePath;
    std::string gameArgs;
    std::string gvtsPath;
    std::string checkPath;
    for (size_t i = 0; i < args.size(); ++i) {
        if ((args[i] == "--gpk" || args[i] == "--game") && i + 1 < args.size()) {
            gamePath = args[++i];
        } else if (args[i] == "--gvts" && i + 1 < args.size()) {
            gvtsPath = args[++i];
        } else if (args[i] == "-gvtsvm" && i + 1 < args.size()) {
            checkPath = args[++i];
        } else if (args[i] == "--args" && i + 1 < args.size()) {
            gameArgs = args[++i];
        } else if (!args[i].empty() && args[i][0] != '-' && gamePath.empty() && gvtsPath.empty()) {
            //无开关的裸参数：以 .gvts 结尾当脚本，其余当游戏包。这样把文件拖到 exe 上就能运行。
            std::string lower = args[i];
            for (size_t k = 0; k < lower.size(); ++k) lower[k] = (char)tolower((unsigned char)lower[k]);
            if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".gvts") == 0) gvtsPath = args[i];
            else gamePath = args[i];
        }
    }

    //-gvtsvm 模式：只做语法校验后立即返回，不开窗口。用于编辑器或 CI 里检查脚本。
    //返回值来自校验函数：通常 0 表示通过，非 0 为错误码，直接作为进程退出码。
    if (!checkPath.empty()) {
        typedef int (*CheckFn)(const char*, char*, int);
        auto checkFn = (CheckFn)GetProcAddress(engine, "Gvtcier_CheckGvts");
        if (!checkFn) {
            outLine("[host] 引擎缺少 Gvtcier_CheckGvts 导出\n");
            FreeLibrary(engine);
            return 1;
        }
        std::string abs = Gvtcier_W32_FullPathUtf8(checkPath.c_str());
        char err[2048];
        int rc = checkFn(abs.empty() ? checkPath.c_str() : abs.c_str(), err, (int)sizeof(err));
        outLine("[host] gvtsvm returned %d\n", rc);
        FreeLibrary(engine);
        return rc;
    }

    if (!gvtsPath.empty()) {
        //裸脚本模式：先解析绝对路径并确认文件存在，再切换工作目录到脚本所在目录。
        //切目录很关键：脚本里的相对路径资源（贴图、音频）都相对脚本位置解析，不切就会从 exe 目录找。
        std::string gvtsAbs = Gvtcier_W32_FullPathUtf8(gvtsPath.c_str());
        if (gvtsAbs.empty() || !Gvtcier_W32_FileExistsUtf8(gvtsAbs.c_str())) {
            errBox("Gvtcier", "找不到 Gvts 脚本。\n\n路径：%s", gvtsPath.c_str());
            FreeLibrary(engine);
            return 1;
        }
        std::wstring gvtsWide = Gvtcier_W32_ToWide(gvtsAbs);
        size_t sl = gvtsWide.find_last_of(L"\\/");
        if (sl != std::wstring::npos) SetCurrentDirectoryW(gvtsWide.substr(0, sl).c_str());
        outLine("[host] gvts = %s\n", gvtsAbs.c_str());
        int rc = runFn(engine, gvtsAbs.c_str(), gameArgs.empty() ? nullptr : gameArgs.c_str());
        outLine("[host] engine returned %d\n", rc);
        FreeLibrary(engine);
        return rc;
    }

    //打包出来的游戏没有命令行参数：exe 旁边是同名的 xxx_Data 目录，游戏包就在里面。
    //先按 exe 名找 <exe名>_Data，找不到再看有没有唯一的 *_Data，这样玩家双击就能开玩。
    if (gamePath.empty() && gvtsPath.empty()) {
        const std::string dataDir = findGameData(hostDir);
        if (!dataDir.empty()) {
            gamePath = findGpkIn(dataDir);
            if (gamePath.empty()) {
                errBox("Gvtcier", "%s 里没有找到 .gpk 游戏包。\n\n请确认打包时选中了游戏包。", dataDir.c_str());
                FreeLibrary(engine);
                return 1;
            }
            outLine("[host] auto game = %s\n", gamePath.c_str());
        }
    }

    if (gamePath.empty()) {
        //没给游戏时打印用法说明并返回非 0，避免玩家双击 exe 后看到黑屏不知所以。
        outLine("Gvtcier 引擎宿主\n");
        outLine("用法：\n");
        outLine("  main.exe <游戏.gpk|游戏.gvts>   直接运行游戏\n");
        outLine("  main.exe --gvts <脚本.gvts>     以裸脚本模式运行\n");
        outLine("  main.exe --gpk <游戏.gpk>       显式指定游戏包\n");
        outLine("  main.exe -gvtsvm <脚本.gvts>    仅校验脚本语法\n");
        FreeLibrary(engine);
        return 1;
    }

    std::string gameAbs = Gvtcier_W32_FullPathUtf8(gamePath.c_str());
    if (gameAbs.empty()) {
        errBox("Gvtcier", "无法解析游戏包路径：\n%s", gamePath.c_str());
        FreeLibrary(engine);
        return 1;
    }
    outLine("[host] game = %s\n", gameAbs.c_str());

    if (!Gvtcier_W32_FileExistsUtf8(gameAbs.c_str())) {
        errBox("Gvtcier",
               "找不到游戏包。\n\n"
               "路径：%s\n\n"
               "游戏包应为 游戏名.gpk（内含身份信息、封面、脚本与全部资源）。",
               gameAbs.c_str());
        FreeLibrary(engine);
        return 1;
    }

    {
        //与裸脚本模式同理：把工作目录切到游戏包所在目录，游戏内的相对路径资源才能正确找到。
        std::wstring w = Gvtcier_W32_ToWide(gameAbs);
        size_t sl = w.find_last_of(L"\\/");
        if (sl != std::wstring::npos) SetCurrentDirectoryW(w.substr(0, sl).c_str());
    }

    //调用引擎跑游戏：第一个参数传引擎自身模块句柄，引擎据此定位资源与日志目录。
    //引擎返回退出码，启动器原样返回；返回前释放引擎模块，避免退出阶段的内存与句柄清理问题。
    int rc = runFn(engine, gameAbs.c_str(), gameArgs.empty() ? nullptr : gameArgs.c_str());
    outLine("[host] engine returned %d\n", rc);
    FreeLibrary(engine);
    return rc;
}
