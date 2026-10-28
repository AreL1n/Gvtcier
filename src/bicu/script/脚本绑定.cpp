#include "Gvtcier.h"
#include <cstdlib>

// 脚本预处理第一步：把 `当二维开始时{` 这种"标题紧跟左括号"的写法拆开。
// 做法是在左括号前插一个换行，让标题独占一行。为什么需要：
// 后面的 checkScriptShape 是逐行判断的，标题和左括号挤在一行时，
// 行首确实以标题开头能过，但如果写成 `某函数(){ 当三维开始时{`，标题就不在行首了，
// 逐行扫描就认不出来。这一步先做无损拆分，后面的所有扫描都受益。
static void splitStartHeaders(std::string& code) {
    const char* headers[2] = { u8"当二维开始时", u8"当三维开始时" };
    for (int h = 0; h < 2; ++h) {
        size_t hp = code.find(headers[h]);
        while (hp != std::string::npos) {
            size_t ob = code.rfind('{', hp);
            size_t after = hp + std::strlen(headers[h]);
            while (after < code.size() && (code[after] == ' ' || code[after] == '\t')) ++after;
            if (ob != std::string::npos && ob < after) {
                code.insert(ob, "\n");
                hp = code.find(headers[h], hp + 1);
                continue;
            }
            hp = code.find(headers[h], after);
        }
    }
}

// 跳过一个字符串/字符字面量，返回字面量之后的位置。i 指向引号本身。
// 遇到反斜杠就跳过两个字符，这样 \" 不会被误认为结尾。
// 之所以要跳字面量：函数体匹配括号时，字符串里的 { } ( ) 不能算进深度，
// 否则一个 "{" 字符串就能把整段的括号计数带偏。
static size_t skipLiteral(const std::string& s, size_t i) {
    char q = s[i];
    ++i;
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == q) return i + 1;
        ++i;
    }
    return s.size();
}

// 跳过一行 // 注释，返回下一行开头。只认 //，因为脚本里 "--" 已经被 fixGameStartBlock 处理过了。
static size_t skipComment(const std::string& s, size_t i) {
    if (i + 1 >= s.size() || s[i + 1] != '/') return i;
    size_t e = s.find('\n', i);
    return (e == std::string::npos) ? s.size() : e + 1;
}

// 找出脚本里所有名为 name 的函数体的左花括号与右花括号位置，写进 opens/closes。
// 这是模组覆盖功能的定位器，扫描步骤：
//   1) 找"函数"关键字，跳过空白与 UTF-8 首字节(0xEF)，读出紧随其后的函数名；
//   2) 名字必须完全等于 name（长度也要一致，否则"绘制"会匹配到"绘制世界线"）；
//   3) 找到参数表的括号对（跳过字面量与注释），再找后面的 {；
//   4) 从 { 开始配平花括号找到对应的 }。
// 全程跳过字符串与注释，避免里面的括号干扰深度计数。
// 返回是否至少找到一个。同名函数可以被定义多次，所以用数组返回。
static bool findFunctionBlocks(const std::string& code, const std::string& name,
                               std::vector<size_t>& opens, std::vector<size_t>& closes) {
    std::string key = u8"函数";
    size_t pos = 0;
    while ((pos = code.find(key, pos)) != std::string::npos) {
        size_t i = pos + key.size();
        bool head = false;
        while (i < code.size()) {
            char c = code[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
            if (c == '\xEF') { i += 3; continue; }
            head = true;
            break;
        }
        if (!head) break;
        size_t nameStart = i;
        while (i < code.size()) {
            char c = code[i];
            if (c == '(' || c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
            if (c == '\xEF') { i += 3; continue; }
            ++i;
        }
        if (code.compare(nameStart, name.size(), name) != 0 || i - nameStart != name.size()) {
            pos = nameStart;
            continue;
        }
        while (i < code.size() && (code[i] == ' ' || code[i] == '\t' || code[i] == '\r' || code[i] == '\n')) ++i;
        if (i >= code.size() || code[i] != '(') { pos = nameStart; continue; }
        int depth = 0;
        bool found = false;
        size_t openAt = 0;
        while (i < code.size()) {
            char c = code[i];
            if (c == '"' || c == '\'') { i = skipLiteral(code, i); continue; }
            if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') { i = skipComment(code, i); continue; }
            if (c == '(') ++depth;
            else if (c == ')') {
                --depth;
                if (depth == 0) { ++i; break; }
            }
            ++i;
        }
        while (i < code.size()) {
            char c = code[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
            if (c == '{') { openAt = i; found = true; }
            break;
        }
        if (!found) { pos = nameStart; continue; }
        depth = 0;
        size_t closeAt = 0;
        bool closed = false;
        i = openAt;
        while (i < code.size()) {
            char c = code[i];
            if (c == '"' || c == '\'') { i = skipLiteral(code, i); continue; }
            if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') { i = skipComment(code, i); continue; }
            if (c == '{') ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0) { closeAt = i; closed = true; break; }
            }
            ++i;
        }
        if (!closed) { pos = nameStart; continue; }
        opens.push_back(openAt);
        closes.push_back(closeAt);
        pos = closeAt + 1;
    }
    return !opens.empty();
}

// 取路径里的文件名并去掉扩展名，如 "mods/_override/计分.gvts" -> "计分"。
// 模组覆盖就是用文件名当函数名：覆盖片段 hello.gvts 会替换脚本里名为 hello 的函数体。
static std::string fileNameOfMod(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    std::string n = (s == std::string::npos) ? path : path.substr(s + 1);
    size_t d = n.rfind('.');
    return (d == std::string::npos) ? n : n.substr(0, d);
}

// 应用模组函数覆盖：遍历资源里 _override/ 目录下的所有片段，
// 每个片段的内容整体替换脚本里同名函数的函数体（不含花括号）。
// 从后往前替换，这样前面的位置不会因为后面内容的长度变化而失效。
// 找不到对应函数时只打一行日志、跳过，不算错误。
// 返回是否真的替换过（供调试判断）。
static bool applyModFunctionOverrides(std::string& code) {
    std::vector<std::string> list;
    if (Gvtcier_Res_ListMod("_override/", list) <= 0) return false;
    bool any = false;
    for (size_t k = 0; k < list.size(); ++k) {
        std::string name = fileNameOfMod(list[k]);
        if (name.empty()) continue;
        std::vector<unsigned char> raw;
        if (!Gvtcier_Res_ReadFile(list[k].c_str(), raw) || raw.empty()) continue;
        std::string body(raw.begin(), raw.end());
        std::vector<size_t> opens;
        std::vector<size_t> closes;
        if (!findFunctionBlocks(code, name, opens, closes)) {
            printf("[mod] 覆盖片段无对应函数: %s\n", name.c_str());
            continue;
        }
        for (size_t b = opens.size(); b-- > 0;) {
            code.replace(opens[b] + 1, closes[b] - opens[b] - 1, "\n" + body + "\n");
        }
        printf("[mod] 已覆盖函数: %s (%d 处)\n", name.c_str(), (int)opens.size());
        any = true;
    }
    return any;
}

// 跳过空白与 // 注释，用于解析 mod.ghook 这种一行一条的补丁指令。
static void skipHookSpace(const std::string& s, size_t& i) {
    while (i < s.size()) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            size_t e = s.find('\n', i);
            i = (e == std::string::npos) ? s.size() : e + 1;
            continue;
        }
        break;
    }
}

// 解析一个带引号的字符串，支持 \n、\t 与任意 \x 转义（其他转义只取字符本身）。
// 解析成功时 i 停在右引号之后，out 是解出来的内容；没遇到引号或引号没闭合返回 false。
static bool readHookString(const std::string& s, size_t& i, std::string& out) {
    skipHookSpace(s, i);
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') out.push_back('\n');
            else if (n == 't') out.push_back('\t');
            else out.push_back(n);
            i += 2;
            continue;
        }
        if (c == '"') { ++i; return true; }
        out.push_back(c);
        ++i;
    }
    return false;
}

// 应用 mod.ghook 里的文本补丁。语法是 `替换 "原文" 为 "新文"`，可以写多条。
// 每条只替换脚本里第一次出现的原文，命中后打一行日志并写引擎日志；
// 没命中同样记日志，方便模组作者排查版本不匹配。
// 注意这是纯文本替换，不区分是否落在字符串/注释里，也不校验语法，模组要自己保证替换后仍然合法。
static bool applyModHooks(std::string& code) {
    std::vector<unsigned char> raw;
    if (!Gvtcier_Res_ReadFile("mod.ghook", raw) || raw.empty()) return false;
    std::string text(raw.begin(), raw.end());
    bool any = false;
    size_t i = 0;
    std::string kwReplace = u8"替换";
    std::string kwWith = u8"为";
    while (true) {
        skipHookSpace(text, i);
        if (i >= text.size()) break;
        if (text.compare(i, kwReplace.size(), kwReplace) != 0) break;
        i += kwReplace.size();
        std::string from;
        if (!readHookString(text, i, from)) break;
        skipHookSpace(text, i);
        if (text.compare(i, kwWith.size(), kwWith) != 0) break;
        i += kwWith.size();
        std::string to;
        if (!readHookString(text, i, to)) break;
        if (from.empty()) continue;
        size_t at = code.find(from);
        if (at == std::string::npos) {
            printf("[mod] 补丁未命中: %s\n", from.c_str());
            rlog::Gvtcier_Log_Write("[mod] 补丁未命中\n");
            continue;
        }
        code.replace(at, from.size(), to);
        printf("[mod] 已打补丁 %s -> %s\n", from.c_str(), to.c_str());
        rlog::Gvtcier_Log_Write("[mod] 已打补丁\n");
        any = true;
    }
    return any;
}

// 初始化脚本虚拟机。只做一次：建 gvts 状态机并把标准库换成中文名版本（见 openGameLibs）。
// 返回值表示虚拟机是否可用。
bool GvtsBind::init() {
    if (state_) return true;
    gvts_State* L = gvtsG_newstate();
    if (!L) return false;
    openGameLibs(L);
    state_ = L;
    return true;
}

// 编译并运行一份脚本源码，这是脚本总入口，串起整条预处理流水线：
//   1) applyGvtAttr 抽出 @Gvtcier 属性行；
//   2) processIncludes 展开 包含/从这里执行；
//   3) applyModFunctionOverrides + applyModHooks 应用模组覆盖与补丁；
//   4) fixGameStartBlock + splitStartHeaders + rewriteBlockHeaders 做语法糖改写；
//   5) checkScriptShape 做结构性检查（只允许主循环、全局、函数、顶层调用）；
//   6) 首次运行时注册全部引擎 API（registerGvtApi），之后复用；
//   7) loadbuffer + pcall 执行，失败时翻译错误信息、打印并返回 false。
// 成功后还会按脚本里是否有 __gvt_mainloop_2d/_3d 切换 2D/3D 模式，并加载模组脚本。
// chunkName_ 用 "@" 前缀是因为 gvts 把 @ 开头的块名当文件名，报错时不加"字符串"字样。
bool GvtsBind::run(const std::string& code) {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return false;
    GvtcierError::clearGvtsError();
    modLoaded_ = false;
    hookLoaded_ = false;
    std::string exec = code;
    applyGvtAttr(exec);
    bool gvtWanted = false;
    std::string incErr;
    if (!processIncludes(exec, gvtWanted, incErr)) {
        GvtcierError::gvtsError(incErr.c_str());
        printf("[GvtcierGvts] %s\n", incErr.c_str());
        return false;
    }
    applyModFunctionOverrides(exec);
    applyModHooks(exec);
    fixGameStartBlock(exec);
    splitStartHeaders(exec);
    rewriteBlockHeaders(exec);
    {
        std::string shapeErr;
        if (!checkScriptShape(exec, shapeErr)) {
            GvtcierError::gvtsError(shapeErr.c_str());
            printf("[GvtcierGvts] %s\n", shapeErr.c_str());
            return false;
        }
    }
    if (!gvtEnabled_) {
        registerGvtApi(L, &ctx_);
        gvtEnabled_ = true;
    }
    std::string chunk = "@" + chunkName_;
    if (gvtsG_loadbuffer(L, exec.c_str(), exec.size(), chunk.c_str()) != GVTS_OK
        || gvts_pcall(L, 0, GVTS_MULTRET, 0) != GVTS_OK) {
        const char* raw = gvts_tostring(L, -1);
        GvtcierError::gvtsError(raw);
        printf("[GvtcierGvts] %s\n", GvtcierError::translateGvts(raw).c_str());
        GvtcierError::warnLanguage(exec.c_str());
        gvts_pop(L, 1);
        return false;
    }
    GvtcierError::warnLanguage(exec.c_str());
    applyMainLoopMode();
    loadModScript();
    return true;
}

// 加载并运行模组脚本 _mod.gvts。只跑一次（modLoaded_ 标记，先置位再干活，避免失败后反复重试刷日志）。
// 模组脚本也走 包含 展开，但没有形状检查，也没有 @Gvtcier 属性与 hook 处理，
// 目的是让模组能自由 hook 主脚本导出的全局函数。
// 返回是否成功执行（文件不存在算失败，但会被静默吞掉，因为大部分游戏没有模组）。
bool GvtsBind::loadModScript() {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return false;
    if (modLoaded_) return true;
    modLoaded_ = true;
    std::vector<unsigned char> raw;
    if (!Gvtcier_Res_ReadFile("_mod.gvts", raw) || raw.empty()) return false;
    std::string exec(raw.begin(), raw.end());
    bool gvtWanted = false;
    std::string incErr;
    if (!processIncludes(exec, gvtWanted, incErr)) {
        printf("[mod] 模组脚本包含处理失败: %s\n", incErr.c_str());
        rlog::Gvtcier_Log_Write("[mod] 模组脚本包含处理失败: %s\n", incErr.c_str());
        return false;
    }
    if (gvtsG_loadbuffer(L, exec.c_str(), exec.size(), "@_mod.gvts") != GVTS_OK) {
        const char* raw2 = gvts_tostring(L, -1);
        printf("[mod] 模组脚本编译失败: %s\n", GvtcierError::translateGvts(raw2).c_str());
        rlog::Gvtcier_Log_Write("[mod] 模组脚本编译失败\n");
        gvts_pop(L, 1);
        return false;
    }
    if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
        const char* raw2 = gvts_tostring(L, -1);
        printf("[mod] 模组脚本运行失败: %s\n", GvtcierError::translateGvts(raw2).c_str());
        rlog::Gvtcier_Log_Write("[mod] 模组脚本运行失败\n");
        gvts_pop(L, 1);
        return false;
    }
    printf("[mod] 模组脚本已应用 (%d 字节)\n", (int)exec.size());
    rlog::Gvtcier_Log_Write("[mod] 模组脚本已应用 (%d 字节)\n", (int)exec.size());
    return true;
}

// 调用一个零参数的脚本钩子函数（钩子_每帧/载入/绘制 之类）。
// 不像 call() 那样返回成败，钩子失败只打日志不进错误状态，避免一个坏钩子把整局卡死。
// 全局变量不是函数时直接返回，所以脚本不写钩子也没有任何开销。
void GvtsBind::callHook(const char* name) {
    gvts_State* L = (gvts_State*)state_;
    if (!L || !name) return;
    gvts_getglobal(L, name);
    if (!gvts_isfunction(L, -1)) { gvts_pop(L, 1); return; }
    if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
        printf("[mod] 钩子 %s 失败: %s\n", name, gvts_tostring(L, -1));
        gvts_pop(L, 1);
    }
}

// 根据脚本里定义了哪个主循环函数来决定运行模式：有 __gvt_mainloop_2d 就是 2D，有 _3d 就是 3D。
// 两者都有时 2D 优先。切换全局模式后还要同步告诉渲染器，让它按对应路径渲染。
void GvtsBind::applyMainLoopMode() {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return;
    gvts_getglobal(L, "__gvt_mainloop_2d");
    bool has2D = gvts_isfunction(L, -1);
    gvts_pop(L, 1);
    gvts_getglobal(L, "__gvt_mainloop_3d");
    bool has3D = gvts_isfunction(L, -1);
    gvts_pop(L, 1);
    if (has2D) {
        Gvtcier_Gvtio_Use2D();
        if (ctx_.renderer) ctx_.renderer->Gvtcier_Render_Set2DMode(true);
    } else if (has3D) {
        Gvtcier_Gvtio_Use3D();
        if (ctx_.renderer) ctx_.renderer->Gvtcier_Render_Set2DMode(false);
    }
}

// 按名字调用一个无参全局函数，返回是否真的调用成功。
// 与 callHook 的差别是这里会把错误进到 GvtcierError 并打印，用于引擎主动触发的回调。
bool GvtsBind::call(const std::string& fn) {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return false;
    GvtcierError::clearGvtsError();
    gvts_getglobal(L, fn.c_str());
    if (gvts_isfunction(L, -1)) {
        if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
            GvtcierError::gvtsError(gvts_tostring(L, -1)); printf("[GvtcierGvts] %s\n", GvtcierError::translateGvts(gvts_tostring(L, -1)).c_str());
            gvts_pop(L, 1);
            return false;
        }
        return true;
    }
    gvts_pop(L, 1);
    return false;
}

// 启动一个协程来跑脚本函数，这是"等待 N 秒""每帧"这类主循环的实现基础。
// 流程：取全局函数 -> 建线程 -> 把函数挪进线程 -> 用注册表引用锁住它 -> resume。
//   resume 后若是 yield，说明脚本调了 执行_等待，让出的第一个返回值就是等待秒数，
//   于是把线程连同剩余秒数 push 进 waitCos 队列，由 tick 每帧倒计时后续跑；
//   resume 后直接结束，就解引用并返回 nullptr。
// nargs>0 时会给协程传一个参数（当前是上一帧 dt），供 刻 这类带参回调使用。
// 返回协程指针（其实就是 gvts_State*），挂起时由调用方保存；未挂起或失败返回 nullptr。
void* GvtsBind::startCo(const std::string& fn, int nargs) {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return nullptr;
    gvts_getglobal(L, fn.c_str());
    if (!gvts_isfunction(L, -1)) {
        gvts_pop(L, 1);
        return nullptr;
    }
    gvts_State* co = gvts_newthread(L);
    gvts_pushvalue(L, -2);
    gvts_xmove(L, co, 1);
    int coRef = gvtsG_ref(L, GVTS_REGISTRYINDEX);
    gvts_pop(L, 1);
    if (nargs > 0) gvts_pushnumber(co, ctx_.lastDt);
    int nres = 0;
    int st = gvts_resume(co, L, nargs, &nres);
    if (st == GVTS_YIELD) {
        double wait = nres > 0 ? gvts_tonumber(L, -1) : 0.0;
        gvts_pop(L, nres);
        ctx_.waitCos.push_back({ (void*)co, wait, coRef });
        return (void*)co;
    }
    if (st != GVTS_OK) {
        const char* er = gvts_tostring(co, -1);
        GvtcierError::gvtsError(er ? er : u8"未知错误");
        printf("[GvtcierGvts] %s\n", GvtcierError::translateGvts(er ? er : u8"未知错误").c_str());
        gvts_pop(L, 1);
    } else {
        if (nres > 0) gvts_pop(L, nres);
    }
    gvtsG_unref(L, GVTS_REGISTRYINDEX, coRef);
    return nullptr;
}

// 继续一个之前挂起的协程（等待时间到了由 tick 调进来）。
// 先在 waitCos 里找到它对应的注册表引用号（ref），因为 resume 结束时要把引用解掉，否则函数泄漏。
// 继续时不再传参数，所以等待返回后脚本里拿到的后续参数是 nil。
// 再次 yield 就重新入队（带新的等待秒数），彻底结束则解引用并返回 false。
bool GvtsBind::resumeCo(void* thread) {
    gvts_State* L = (gvts_State*)state_;
    if (!L || !thread) return false;
    int ref = -1;
    for (size_t i = 0; i < ctx_.waitCos.size(); ++i) {
        if (ctx_.waitCos[i].thread == thread) { ref = ctx_.waitCos[i].ref; break; }
    }
    gvts_State* co = (gvts_State*)thread;
    int nres = 0;
    int st = gvts_resume(co, L, 0, &nres);
    if (st == GVTS_YIELD) {
        double wait = nres > 0 ? gvts_tonumber(L, -1) : 0.0;
        gvts_pop(L, nres);
        ctx_.waitCos.push_back({ thread, wait, ref });
        return true;
    }
    if (st != GVTS_OK) {
        const char* er = gvts_tostring(co, -1);
        GvtcierError::gvtsError(er ? er : u8"未知错误");
        printf("[GvtcierGvts] %s\n", GvtcierError::translateGvts(er ? er : u8"未知错误").c_str());
        gvts_pop(L, 1);
    } else if (nres > 0) {
        gvts_pop(L, nres);
    }
    if (ref >= 0) gvtsG_unref(L, GVTS_REGISTRYINDEX, ref);
    return false;
}

// 关闭脚本层。顺序上有讲究：先销毁音频槽位（否则音频源里引用的资源会悬空），
// 再清掉所有还挂在 ctx_ 里的脚本侧回调（触发器、广播、协程、计时器、渐变），
// 最后关虚拟机。清 ctx_ 必须在 gvts_close 之前，因为关闭后这些引用号就没意义了。
void GvtsBind::shutdown() {
    gvtsResetAudioSlots();
    if (state_) {
        gvts_State* L = (gvts_State*)state_;
        ctx_.trigEnterCb.clear();
        ctx_.trigExitCb.clear();
        ctx_.broadcasts.clear();
        ctx_.waitCos.clear();
        ctx_.timers.clear();
        ctx_.tweens.clear();
        modLoaded_ = false;
        hookLoaded_ = false;
        (void)L;
        gvts_close(L);
        state_ = nullptr;
    }
}

// 从文件加载并运行游戏主脚本。path 为空或读不出来时返回 false。
// chunkName_ 取路径里最后一个 / 或 \ 之后的部分，作为报错时的文件名，同时决定脚本名是否显示完整路径。
bool GvtsBind::loadGame(const char* path) {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return false;
    if (path) {
        std::string code;
        bool ok = Gvtcier_File_GopenText(path, code) && !code.empty();
        printf("[GvtcierGvts] loaded %s (%d bytes, ok=%d)\n", path, (int)code.size(), (int)ok);
        const char* slash = strrchr(path, '/');
        const char* bslash = strrchr(path, '\\');
        if (bslash && (!slash || bslash > slash)) slash = bslash;
        chunkName_ = slash ? (slash + 1) : path;
        if (ok) return run(code);
        printf("[GvtcierGvts] %s\n", GvtcierError::translateGvts("cannot open file").c_str());
    }
    return false;
}

// 驱动"物体脚本"：场景里每个设置了 scriptPath 的物体，都会跑一份独立的脚本实例。
// 首次遇到某物体的脚本时：
//   1) 从资源读出源码；
//   2) 给它建一个私有环境表，__index 指向全局表（所以能调用引擎 API 与全局函数）、
//      __mode 设为 "v"（弱值），弱引用保证物体销毁后环境能被回收；
//   3) 编译执行一遍完成初始化，再把环境自身存成注册表引用，之后每帧复用；
//   4) 用 gvts_setupvalue 把环境设成块的第 1 个上值，脚本顶层就能用局部变量存状态。
// 之后每帧取该环境里的"刻"函数，传入 dt 调一次。
// 已加载的脚本按 (物体 ID, 脚本路径) 匹配，路径变了会重新加载一份新的，旧的引用不做清理。
void GvtsBind::tickObjectScripts(Scene& scene, float dt) {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return;
    scene.ForEachObject([&](Object& o, GvtId<Object> id) {
        if (o.scriptPath.empty()) return;
        int ref = -1;
        for (size_t s = 0; s < objScriptObj_.size(); ++s)
            if (objScriptObj_[s] == id && objScriptPath_[s] == o.scriptPath) ref = objScriptRefs_[s];
        if (ref < 0) {
            std::vector<unsigned char> raw;
            if (!Gvtcier_Res_ReadFile(o.scriptPath.c_str(), raw) || raw.empty()) {
                printf("[gvts] 物体脚本打不开: %s\n", o.scriptPath.c_str());
                return;
            }
            std::string code(raw.begin(), raw.end());
            gvts_newtable(L);
            gvts_newtable(L);
            gvts_rawgeti(L, GVTS_REGISTRYINDEX, GVTS_RIDX_GLOBALS);
            gvts_setfield(L, -2, "__index");
            gvts_pushliteral(L, "v");
            gvts_setfield(L, -2, "__mode");
            gvts_setmetatable(L, -2);
            if (gvtsG_loadbuffer(L, code.c_str(), code.size(), o.scriptPath.c_str()) != GVTS_OK) {
                printf("[gvts] 物体脚本编译失败: %s\n", gvts_tostring(L, -1));
                gvts_pop(L, 2);
                return;
            }
            gvts_setupvalue(L, -1, 1);
            if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
                printf("[gvts] 物体脚本运行失败: %s\n", gvts_tostring(L, -1));
                gvts_pop(L, 2);
                return;
            }
            ref = gvtsG_ref(L, GVTS_REGISTRYINDEX);
            objScriptRefs_.push_back(ref);
            objScriptObj_.push_back(id);
            objScriptPath_.push_back(o.scriptPath);
        }
        gvts_rawgeti(L, GVTS_REGISTRYINDEX, ref);
        gvts_getfield(L, -1, u8"刻");
        if (gvts_isfunction(L, -1)) {
            gvts_pushnumber(L, (double)dt);
            if (gvts_pcall(L, 1, 0, 0) != GVTS_OK) {
                printf("[gvts] 物体脚本 刻 失败: %s\n", gvts_tostring(L, -1));
                gvts_pop(L, 1);
            }
        } else {
            gvts_pop(L, 1);
        }
        gvts_pop(L, 1);
    });
}

// 逐个调用所有物体脚本里的"绘制"函数，供物体的自定义绘制阶段使用。
// 顺序就是加载顺序，不按 z 排序，所以跨物体的遮挡关系由物体脚本自己保证。
void GvtsBind::drawObjectScripts() {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return;
    for (size_t s = 0; s < objScriptRefs_.size(); ++s) {
        gvts_rawgeti(L, GVTS_REGISTRYINDEX, objScriptRefs_[s]);
        gvts_getfield(L, -1, u8"绘制");
        if (gvts_isfunction(L, -1)) {
            if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
                printf("[gvts] 物体脚本 绘制 失败: %s\n", gvts_tostring(L, -1));
                gvts_pop(L, 1);
            }
        } else {
            gvts_pop(L, 1);
        }
        gvts_pop(L, 1);
    }
}

// 每帧的脚本驱动，顺序很关键：
//   1) 记录 dt、累计运行时间、帧号，调 钩子_每帧；
//   2) 把 waitCos 里所有等待协程的剩余时间减 dt，到点的拿出来续跑（边遍历边删，用下标控制）；
//   3) 计时器、触发器、UI 控件状态各 tick 一次；
//   4) 如果没有主循环函数（脚本用的是"刻"风格），确保"刻"协程在跑，没在等就再启一个；
//   5) 第 2 帧才调 钩子_载入，留一帧让脚本完成初始化。
// 之所以第 2 帧才载入钩子：脚本顶层变量要先执行完，钩子才有东西可读。
void GvtsBind::tick(Scene& scene, float dt) {
    (void)scene;
    gvts_State* L = (gvts_State*)state_;
    if (!L) return;
    ctx_.lastDt = dt;
    ctx_.elapsed += (double)dt;
    ctx_.frameCount++;
    callHook(u8"钩子_每帧");

    for (size_t i = 0; i < ctx_.waitCos.size();) {
        ctx_.waitCos[i].remain -= (double)dt;
        if (ctx_.waitCos[i].remain > 0.0) { ++i; continue; }
        void* th = ctx_.waitCos[i].thread;
        ctx_.waitCos.erase(ctx_.waitCos.begin() + i);
        resumeCo(th);
    }

    Gvtcier_Time_Tick(&ctx_, dt);
    Gvtcier_Trigger_Tick(L, &ctx_);
    Gvtcier_Ui_Tick(&ctx_);

    bool tickWait = false;
    for (size_t i = 0; i < ctx_.waitCos.size(); ++i) {
        if (ctx_.waitCos[i].thread == tickCo_) { tickWait = true; break; }
    }
    if (!tickWait) {
        gvts_getglobal(L, mainLoopName());
        bool hasMain = gvts_isfunction(L, -1);
        gvts_pop(L, 1);
        if (!hasMain) {
            tickCo_ = startCo(u8"刻", 1);
        }
    }
    if (!hookLoaded_ && ctx_.frameCount >= 2) {
        hookLoaded_ = true;
        callHook(u8"钩子_载入");
    }
}

// 当前该用的主循环函数名，取决于渲染器是否处于 2D 模式。
const char* GvtsBind::mainLoopName() const {
    return ctx_.renderer && ctx_.renderer->Gvtcier_Render_Is2DMode()
        ? "__gvt_mainloop_2d" : "__gvt_mainloop_3d";
}

// 每帧的绘制驱动，优先走"主循环"协程：
//   - 有 __gvt_mainloop_2d/_3d 时，用一个专用协程来跑它（如果它没在等待状态就重新启动），
//     这样主循环里可以用 执行_等待 做分帧，不用写成状态机；
//   - 没有主循环就退回调用全局"绘制"函数，同样没有就什么都不画。
// 无论走哪条路，最后都会调一次 钩子_绘制，让模组有机会在画面之上补东西。
// 协程版本不会在 pcall 失败时清错误状态，只有直接调函数那条路会。
void GvtsBind::draw(float dt) {
    gvts_State* L = (gvts_State*)state_;
    if (!L) return;
    if (dt > 0.0f) ctx_.lastDt = dt;
    const char* main = mainLoopName();
    gvts_getglobal(L, main);
    if (gvts_isfunction(L, -1)) {
        gvts_pop(L, 1);
        bool tickWait = false;
        for (size_t i = 0; i < ctx_.waitCos.size(); ++i) {
            if (ctx_.waitCos[i].thread == drawCo_) { tickWait = true; break; }
        }
        if (!tickWait) drawCo_ = startCo(main, 0);
        callHook(u8"钩子_绘制");
        return;
    }
    gvts_pop(L, 1);
    gvts_getglobal(L, u8"绘制");
    if (!gvts_isfunction(L, -1)) {
        gvts_pop(L, 1);
        callHook(u8"钩子_绘制");
        return;
    }
    if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
        GvtcierError::gvtsError(gvts_tostring(L, -1)); printf("[GvtcierGvts] %s\n", GvtcierError::translateGvts(gvts_tostring(L, -1)).c_str());
        gvts_pop(L, 1);
    }
    callHook(u8"钩子_绘制");
}

// 析构时顺手关闭脚本层，保证虚拟机与音频源不泄漏。
GvtsBind::~GvtsBind() { shutdown(); }

// 脚本语法体检（给编辑器/工具用，不影响游戏运行）。
// 返回 0 表示通过，1 表示脚本内容有问题（含 包含 错误、形状错误、语法错误），2 表示环境问题（读不到文件、虚拟机建不起来）。
// 做法：跑一遍预处理（但不应用模组覆盖与补丁，体检的是原脚本），然后反复 loadbuffer：
//   gvts 的编译错误一次只报第一个，所以每轮把出错的那一行整行替换成空格再重编，
//   直到编译通过或用完 12 轮上限。逐行涂白是为了继续挖出后面的错误，同时保持行号不变。
//   "第 N 行"这种中文错误串里也带行号，优先用它；同一行重复报错就跳过，避免死循环。
// errOut 用来把第一条错误回传，容量 errCap。
extern "C" GVTCIER_API int Gvtcier_CheckGvts(const char* gvtsPath, char* errOut, int errCap) {
    if (errOut && errCap > 0) errOut[0] = 0;
    if (!gvtsPath || !gvtsPath[0]) return 2;
    std::string code;
    if (!Gvtcier_File_GopenText(gvtsPath, code) || code.empty()) {
        const char* m = u8"读取脚本失败";
        printf("[GvtcierGvts] %s\n", m);
        if (errOut && errCap > 0) snprintf(errOut, errCap, "%s", m);
        return 2;
    }
    std::string exec = code;
    applyGvtAttr(exec);
    bool gvtWanted = false;
    std::string incErr;
    if (!processIncludes(exec, gvtWanted, incErr)) {
        printf("[GvtcierGvts] %s\n", incErr.c_str());
        if (errOut && errCap > 0) snprintf(errOut, errCap, "%s", incErr.c_str());
        return 1;
    }
    fixGameStartBlock(exec);
    splitStartHeaders(exec);
    rewriteBlockHeaders(exec);
    std::string shapeErr;
    if (!checkScriptShape(exec, shapeErr)) {
        printf("[GvtcierGvts] %s\n", shapeErr.c_str());
        if (errOut && errCap > 0) snprintf(errOut, errCap, "%s", shapeErr.c_str());
        return 1;
    }
    gvts_State* L = gvtsG_newstate();
    if (!L) {
        const char* m = u8"虚拟机初始化失败";
        printf("[GvtcierGvts] %s\n", m);
        if (errOut && errCap > 0) snprintf(errOut, errCap, "%s", m);
        return 2;
    }
    int rc = 0;
    std::string work = exec;
    std::vector<std::string> allErrs;
    std::vector<int> seenRoots;
    for (int round = 0; round < 12; ++round) {
        if (gvtsG_loadbuffer(L, work.c_str(), work.size(), "@check.gvts") == GVTS_OK) break;
        const char* raw = gvts_tostring(L, -1);
        std::string tr = GvtcierError::translateGvts(raw ? raw : u8"语法错误");
        gvts_pop(L, 1);
        size_t colon = tr.find(':');
        int ln = (colon != std::string::npos) ? atoi(tr.c_str() + colon + 1) : 0;
        int root = ln;
        {
            size_t key = tr.find(u8"第 ");
            while (key != std::string::npos) {
                size_t numStart = key + strlen(u8"第 ");
                int v = atoi(tr.c_str() + numStart);
                if (v > 0) { root = v; break; }
                key = tr.find(u8"第 ", key + 1);
            }
        }
        bool dup = false;
        for (size_t i = 0; i < seenRoots.size(); ++i) if (seenRoots[i] == root) { dup = true; break; }
        if (dup) { if (root <= 0) break; }
        else { seenRoots.push_back(root); allErrs.push_back(tr); }
        if (root <= 0) break;
        size_t pos = 0;
        int cur = 1;
        while (cur < root) {
            size_t nl = work.find('\n', pos);
            if (nl == std::string::npos) { pos = std::string::npos; break; }
            pos = nl + 1;
            ++cur;
        }
        if (pos == std::string::npos || pos >= work.size()) break;
        size_t nl = work.find('\n', pos);
        size_t end = (nl == std::string::npos) ? work.size() : nl;
        bool blank = true;
        for (size_t i = pos; i < end; ++i) { if (work[i] != ' ' && work[i] != '\t' && work[i] != '\r') { blank = false; break; } }
        for (size_t i = pos; i < end; ++i) work[i] = ' ';
        if (blank) break;
        rc = 1;
    }
    for (size_t i = 0; i < allErrs.size(); ++i) printf("[GvtcierGvts] %s\n", allErrs[i].c_str());
    if (rc != 0 && errOut && errCap > 0 && !allErrs.empty())
        snprintf(errOut, errCap, "%s", allErrs[0].c_str());
    gvts_close(L);
    return rc;
}
