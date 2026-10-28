#include "Gvtcier.h"

// 脚本绑定：核心。最基础的一组脚本接口，包括打印输出、脚本间调用、程序控制等。
// 这里的函数大多不依赖场景，任何阶段调用都安全。

// 打印到控制台与日志文件，脚本里最常用的调试手段
static int g_cr_print(gvts_State* L) {
    const char* s = gvts_tostring(L, 1);
    printf("[gvts] %s\n", s ? s : "");
    rlog::Gvtcier_Log_Write("[gvts] %s", s ? s : "");
    return 0;
}

static int g_cr_save(gvts_State* L) {
    const char* content = gvtsG_checkstring(L, 1);
    const char* fname = gvtsG_checkstring(L, 2);
    const char* dir = gvtsG_optstring(L, 3, "");
    if (!fname || !fname[0]) { gvts_pushboolean(L, 0); return 1; }
    std::string path;
    if (dir && dir[0]) {
        path = dir;
        char last = path.back();
        if (last != '/' && last != '\\') path += "/";
        path += fname;
    } else {
        path = fname;
    }
    size_t len = content ? strlen(content) : 0;
    bool ok = Gvtcier_File_Gsave(path.c_str(), content, len);
    if (!ok) printf("[gvts] 执行_保存失败: %s\n", path.c_str());
    gvts_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int g_asset_exists(gvts_State* L) {
    const char* name = gvtsG_checkstring(L, 1);
    gvts_pushboolean(L, name && name[0] && Gvtcier_Res_Exists(name));
    return 1;
}

static int g_script_include(gvts_State* L) {
    const char* path = gvtsG_checkstring(L, 1);
    std::vector<unsigned char> bytes;
    if (!path || !path[0] || !Gvtcier_Res_ReadFile(path, bytes) || bytes.empty()) {
        gvts_pushnil(L);
        gvts_pushstring(L, u8"脚本不存在");
        return 2;
    }
    if (gvtsG_loadbuffer(L, (const char*)bytes.data(), bytes.size(), path) != GVTS_OK) {
        const char* err = gvts_tostring(L, -1);
        gvts_pushnil(L);
        gvts_pushstring(L, err ? err : u8"语法错误");
        return 2;
    }
    if (gvts_pcall(L, 0, 0, 0) != GVTS_OK) {
        const char* err = gvts_tostring(L, -1);
        gvts_pushnil(L);
        gvts_pushstring(L, err ? err : u8"运行错误");
        return 2;
    }
    gvts_pushboolean(L, 1);
    return 1;
}

static int g_cr_fps(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushnumber(L, ctx->lastDt > 1e-6f ? (double)(1.0f / ctx->lastDt) : 0.0);
    return 1;
}

static int g_cr_setTitle(gvts_State* L) {
    const char* title = gvtsG_checkstring(L, 1);
#if defined(_WIN32)
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx && ctx->win && title) Gvtcier_W32_SetWindowTextUtf8(ctx->win->hwnd(), title);
#else
    (void)title;
#endif
    return 0;
}

static int g_cr_windowSize(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    if (ctx->win) {
        gvts_pushinteger(L, ctx->win->width());
        gvts_pushinteger(L, ctx->win->height());
    } else {
        gvts_pushinteger(L, 0);
        gvts_pushinteger(L, 0);
    }
    return 2;
}

static int g_cr_quit(gvts_State* L) {
#ifdef _WIN32
    PostQuitMessage(0);
#else
    exit(0);
#endif
    return 0;
}

static int g_cr_log(gvts_State* L) {
    const char* t = gvtsG_checkstring(L, 1);
    if (t) rlog::Gvtcier_Log_Write("%s\n", t);
    return 0;
}

static int g_timeDelta(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushnumber(L, ctx->lastDt);
    return 1;
}

static int g_timeElapsed(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushnumber(L, ctx->elapsed);
    return 1;
}

static int g_timeFrame(gvts_State* L) {
    GvtsEngineCtx* ctx = (GvtsEngineCtx*)gvts_touserdata(L, gvts_upvalueindex(1));
    gvts_pushinteger(L, ctx->frameCount);
    return 1;
}

static uint32_t s_randState = 0u;

static uint32_t randStep() {
    if (s_randState == 0u) {
        uint32_t s = (uint32_t)(Gvtcier_NowSeconds() * 1000000.0);
        s ^= (uint32_t)GetTickCount64();
        s ^= (uint32_t)(uintptr_t)&s;
        s_randState = s ? s : 0x9E3779B9u;
    }
    s_randState ^= s_randState << 13;
    s_randState ^= s_randState >> 17;
    s_randState ^= s_randState << 5;
    if (s_randState == 0) s_randState = 1u;
    return s_randState;
}

static int g_randNext(gvts_State* L) {
    gvts_pushnumber(L, (double)(randStep() % 1000000u) / 1000000.0);
    return 1;
}

static int g_randRange(gvts_State* L) {
    float lo = (float)gvtsG_checknumber(L, 1);
    float hi = (float)gvtsG_checknumber(L, 2);
    float t = (float)(randStep() % 1000000u) / 1000000.0f;
    gvts_pushnumber(L, lo + (hi - lo) * t);
    return 1;
}

static int g_randInt(gvts_State* L) {
    long long lo = (long long)gvtsG_checkinteger(L, 1);
    long long hi = (long long)gvtsG_checkinteger(L, 2);
    if (hi < lo) { long long t = lo; lo = hi; hi = t; }
    unsigned long long span = (unsigned long long)(hi - lo) + 1ull;
    if (span <= 1ull) { gvts_pushinteger(L, (gvts_Integer)lo); return 1; }
    gvts_pushinteger(L, (gvts_Integer)(lo + (long long)(randStep() % span)));
    return 1;
}

static int g_randSeed(gvts_State* L) {
    s_randState = (uint32_t)gvtsG_checkinteger(L, 1);
    if (s_randState == 0) s_randState = 0x9E3779B9u;
    return 0;
}

static std::string trimSpaces(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return std::string();
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

static bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

static bool readScriptFile(const std::string& name, std::string& out) {
    std::vector<unsigned char> bytes;
    if (!Gvtcier_Res_ReadFile(name.c_str(), bytes) || bytes.empty()) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

static void stripIncludeLines(std::string& code) {
    const char* marker = u8"包含 <";
    const size_t markerLen = std::strlen(marker);
    size_t p = code.find(marker);
    while (p != std::string::npos) {
        size_t gt = code.find('>', p + markerLen);
        if (gt == std::string::npos) break;
        size_t ls = code.rfind('\n', p == 0 ? 0 : p - 1);
        size_t lineStart = (ls == std::string::npos) ? 0 : ls + 1;
        size_t le = code.find('\n', gt);
        size_t lineEnd = (le == std::string::npos) ? code.size() : le + 1;
        code.erase(lineStart, lineEnd - lineStart);
        p = code.find(marker);
    }
}

static std::vector<std::string> g_includedFiles;

bool gvtsFileIncluded(const char* name) {
    if (!name || !name[0]) return false;
    for (size_t i = 0; i < g_includedFiles.size(); ++i)
        if (g_includedFiles[i] == name) return true;
    return false;
}

bool processIncludes(std::string& code, bool& gvtEnabled, std::string& err) {
    std::vector<std::string> includes;
    std::vector<bool> used;
    std::vector<std::string> contents;
    g_includedFiles.clear();

    const char* marker = u8"包含 <";
    const size_t markerLen = std::strlen(marker);
    size_t p = code.find(marker);
    while (p != std::string::npos) {
        size_t gt = code.find('>', p + markerLen);
        if (gt == std::string::npos) { err = u8"包含 语句缺少 >"; return false; }
        std::string name = trimSpaces(code.substr(p + markerLen, gt - (p + markerLen)));
        if (name.empty()) { err = u8"包含 的文件名为空"; return false; }
        if (name == "Gvtcier") {
            gvtEnabled = true;
        } else {
            bool dup = false;
            for (size_t i = 0; i < includes.size(); ++i) if (includes[i] == name) { dup = true; break; }
            if (dup) { err = u8"重复包含同一文件：" + name; return false; }
            if (!Gvtcier_Res_Exists(name.c_str())) { err = u8"包含的文件不存在：" + name; return false; }
            std::string content;
            if (endsWith(name, ".gvts")) {
                if (!readScriptFile(name, content)) { err = u8"包含的脚本无法读取：" + name; return false; }
                stripIncludeLines(content);
            }
            includes.push_back(name);
            used.push_back(false);
            contents.push_back(content);
            g_includedFiles.push_back(name);
        }
        p = code.find(marker, gt);
    }

    const char* runMarker = u8"从这里执行";
    p = code.find(runMarker);
    while (p != std::string::npos) {
        size_t lp = code.find('(', p);
        if (lp == std::string::npos) { err = u8"从这里执行 缺少 ( 文件名 )"; return false; }
        size_t rp = code.find(')', lp);
        if (rp == std::string::npos) { err = u8"从这里执行 缺少 )"; return false; }
        std::string arg = trimSpaces(code.substr(lp + 1, rp - (lp + 1)));
        if (arg.size() >= 2 && arg.front() == '"' && arg.back() == '"')
            arg = arg.substr(1, arg.size() - 2);
        int found = -1;
        for (size_t i = 0; i < includes.size(); ++i) if (includes[i] == arg) { found = (int)i; break; }
        if (found < 0) { err = u8"从这里执行 的文件未包含：" + arg; return false; }
        used[found] = true;
        size_t ls = code.rfind('\n', p == 0 ? 0 : p - 1);
        size_t lineStart = (ls == std::string::npos) ? 0 : ls + 1;
        size_t lineEnd = rp + 1;
        code.replace(lineStart, lineEnd - lineStart, contents[found]);
        p = code.find(runMarker);
    }

    for (size_t i = 0; i < includes.size(); ++i) {
        if (!used[i]) printf(u8"[gvts] 警告：已包含但未执行的文件 %s\n", includes[i].c_str());
    }

    stripIncludeLines(code);
    return true;
}

static std::string g_gvtAttrCode;

void rewriteBlockHeaders(std::string& code) {
    const char* heads[2] = { u8"当触发器[", u8"设置_广播(" };
    for (int pass = 0; pass < 2; ++pass) {
        const char* h = heads[pass];
        size_t hp = code.find(h);
        while (hp != std::string::npos) {
            size_t ob = code.find('{', hp);
            if (ob == std::string::npos) break;
            size_t cb = code.find('}', ob + 1);
            if (cb == std::string::npos) break;
            std::string region = code.substr(ob + 1, cb - ob - 1);
            std::string name;
            if (pass == 0) {
                size_t q0 = code.find('"', hp + std::strlen(h));
                if (q0 == std::string::npos || q0 > ob) { hp = code.find(h, cb); continue; }
                size_t q1 = code.find('"', q0 + 1);
                if (q1 == std::string::npos || q1 > ob) { hp = code.find(h, cb); continue; }
                name = code.substr(q0 + 1, q1 - q0 - 1);
                size_t ls = code.rfind('\n', hp == 0 ? 0 : hp - 1);
                size_t lineStart = (ls == std::string::npos) ? 0 : ls + 1;
                std::string ind = code.substr(lineStart, hp - lineStart);
                std::string rep = ind + u8"注册_进入触发区(\"" + name + u8"\", 函数() {" + region + u8"});";
                code.replace(lineStart, cb + 1 - lineStart, rep);
            } else {
                size_t q0 = code.find('"', hp + std::strlen(h));
                if (q0 == std::string::npos || q0 > ob) { hp = code.find(h, cb); continue; }
                size_t q1 = code.find('"', q0 + 1);
                if (q1 == std::string::npos || q1 > ob) { hp = code.find(h, cb); continue; }
                name = code.substr(q0 + 1, q1 - q0 - 1);
                size_t ls = code.rfind('\n', hp == 0 ? 0 : hp - 1);
                size_t lineStart = (ls == std::string::npos) ? 0 : ls + 1;
                std::string ind = code.substr(lineStart, hp - lineStart);
                std::string rep = ind + u8"注册_广播(\"" + name + u8"\", 函数() {" + region + u8"});";
                code.replace(lineStart, cb + 1 - lineStart, rep);
            }
            hp = code.find(h);
        }
    }
}

void fixGameStartBlock(std::string& code) {
    const char* headers[2] = { u8"当二维开始时", u8"当三维开始时" };
    for (int h = 0; h < 2; ++h) {
        const char* header = headers[h];
        size_t hp = code.find(header);
        if (hp == std::string::npos) continue;
        size_t ob = code.find('{', hp + std::strlen(header));
        if (ob == std::string::npos) continue;
        size_t cb = code.rfind('}');
        if (cb == std::string::npos || cb <= ob) continue;
        std::string region = code.substr(ob + 1, cb - (ob + 1));
        std::string fixed;
        fixed.reserve(region.size() + 16);
        int depth = 0;
        size_t i = 0;
        while (i < region.size()) {
            char ch = region[i];
            if (ch == '"' || ch == '\'') {
                char quote = ch;
                size_t j = i + 1;
                while (j < region.size() && region[j] != quote) {
                    if (region[j] == '\\' && j + 1 < region.size()) ++j;
                    ++j;
                }
                size_t stop = (j < region.size()) ? j : region.size() - 1;
                fixed.append(region, i, stop - i + 1);
                i = (j < region.size()) ? j + 1 : region.size();
                continue;
            }
            if (ch == '-' && i + 1 < region.size() && region[i + 1] == '-') {
                size_t j = region.find('\n', i);
                if (j == std::string::npos) j = region.size();
                fixed.append(region, i, j - i);
                i = j;
                continue;
            }
            if (ch == '{') { ++depth; fixed += ch; ++i; continue; }
            if (ch == '}') {
                if (depth <= 0) { fixed += ';'; ++i; continue; }
                --depth;
                fixed += ch;
                ++i;
                continue;
            }
            fixed += ch;
            ++i;
        }
        code = code.substr(0, ob + 1) + fixed + "\n}" + code.substr(cb + 1);
    }
}

void applyGvtAttr(std::string& code) {
    const char* marker = u8"@Gvtcier";
    size_t p = code.find(marker);
    while (p != std::string::npos) {
        size_t ls = code.rfind('\n', p == 0 ? 0 : p - 1);
        size_t lineStart = (ls == std::string::npos) ? 0 : ls + 1;
        size_t le = code.find('\n', p);
        size_t lineEnd = (le == std::string::npos) ? code.size() : le;
        std::string seg = code.substr(p + strlen(marker), lineEnd - (p + strlen(marker)));
        size_t b = seg.find_first_not_of(" \t");
        size_t e = seg.find_last_not_of(" \t\r");
        if (b != std::string::npos && e != std::string::npos) seg = seg.substr(b, e - b + 1);
        if (!seg.empty()) g_gvtAttrCode = seg;
        size_t eraseEnd = (le == std::string::npos) ? code.size() : le + 1;
        code.erase(lineStart, eraseEnd - lineStart);
        p = code.find(marker);
    }
}

const std::string& Gvtcier_Gvts_AttrCode() { return g_gvtAttrCode; }

void Gvtcier_Gvts_ClearAttr() { g_gvtAttrCode.clear(); }

static bool lineStartsWithWord(const std::string& line, size_t b, const char* word) {
    size_t n = std::strlen(word);
    if (line.size() - b < n) return false;
    if (line.compare(b, n, word) != 0) return false;
    size_t after = b + n;
    return after >= line.size() || line[after] == ' ' || line[after] == '\t';
}

static bool isFunctionCall(const std::string& line, size_t b) {
    for (size_t i = b; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '(') return true;
        if (ch == ' ' || ch == '\t' || ch == '=') continue;
        return false;
    }
    return false;
}

bool checkScriptShape(const std::string& code, std::string& err) {
    const char* headers[2] = { u8"当二维开始时", u8"当三维开始时" };
    const char* globalKw = u8"全局";
    bool sawMainLoop = false;
    bool inMainLoop = false;
    int depth = 0;
    size_t pos = 0;
    while (pos <= code.size()) {
        size_t nl = code.find('\n', pos);
        std::string line = (nl == std::string::npos) ? code.substr(pos) : code.substr(pos, nl - pos);
        size_t b = line.find_first_not_of(" \t\r");
        if (b != std::string::npos) {
            bool skip = false;
            if (line.compare(b, 2, "//") == 0 || line.compare(b, 2, "--") == 0) skip = true;
            else if (line[b] == '@') skip = true;
            if (skip) {
                if (nl == std::string::npos) break;
                pos = nl + 1;
                continue;
            }
            if (!inMainLoop && depth == 0 && line.compare(b, std::strlen(u8"包含 <"), u8"包含 <") == 0) {
                size_t gt = line.find('>', b);
                if (gt == std::string::npos) {
                    err = u8"包含 语句缺少 >";
                    return false;
                }
                if (nl == std::string::npos) break;
                pos = nl + 1;
                continue;
            }
            bool isHeader = false;
            for (int h = 0; h < 2; ++h) {
                size_t mn = std::strlen(headers[h]);
                if (line.size() - b >= mn && line.compare(b, mn, headers[h]) == 0) isHeader = true;
            }
            if (isHeader) {
                sawMainLoop = true;
                inMainLoop = true;
            }
            else if (!inMainLoop && !lineStartsWithWord(line, b, globalKw)
                     && !lineStartsWithWord(line, b, u8"函数")
                     && !isFunctionCall(line, b)) {
                err = u8"未被许可的内容:请写入主循环" + std::string(headers[1]) + u8"或" + std::string(headers[0]) + u8"内部";
                return false;
            }
        }
        for (size_t i = (b == std::string::npos ? 0 : b); i < line.size(); ++i) {
            char ch = line[i];
            if (ch == '"' || ch == '\'') {
                char q = ch;
                ++i;
                while (i < line.size() && line[i] != q) {
                    if (line[i] == '\\' && i + 1 < line.size()) ++i;
                    ++i;
                }
                continue;
            }
            if (ch == '-' && i + 1 < line.size() && line[i + 1] == '-') break;
            if (ch == '{') ++depth;
            else if (ch == '}') {
                if (depth > 0) --depth;
                if (inMainLoop && depth == 0) inMainLoop = false;
            }
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (!sawMainLoop) {
        err = u8"缺少主循环:" + std::string(headers[1]) + u8"或" + std::string(headers[0]);
        return false;
    }
    return true;
}

void Gvtcier_Bind_Core(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"输出", u8"执行_打印", u8"输出一行日志", g_cr_print);
    regCap(L, ctx, u8"输出", u8"保存_日志", u8"写入引擎日志文件", g_cr_log);
    regCap(L, ctx, u8"系统", u8"获取_帧率", u8"读取当前帧率", g_cr_fps);
    regCap(L, ctx, u8"系统", u8"设置_窗口标题", u8"设置窗口标题文字", g_cr_setTitle);
    regCap(L, ctx, u8"系统", u8"获取_窗口尺寸", u8"读取窗口宽高，返回宽, 高两个值", g_cr_windowSize);
    regCap(L, ctx, u8"系统", u8"执行_退出", u8"请求关闭窗口并退出", g_cr_quit);
    regCap(L, ctx, u8"资产", u8"检测_资源存在", u8"查询资产（包内或磁盘）是否存在：资产路径", g_asset_exists);
    regCap(L, ctx, u8"文件", u8"执行_保存", u8"把内容写入文件：内容, 文件名.格式, 路径（可空）", g_cr_save);
    regCap(L, ctx, u8"时间", u8"获取_帧间隔", u8"上一帧耗时秒数", g_timeDelta);
    regCap(L, ctx, u8"时间", u8"获取_运行秒数", u8"脚本累计运行秒数", g_timeElapsed);
    regCap(L, ctx, u8"时间", u8"获取_帧号", u8"累计帧数", g_timeFrame);
    regCap(L, ctx, u8"随机", u8"获取_随机", u8"返回 0 到 1 之间的随机数", g_randNext);
    regCap(L, ctx, u8"随机", u8"获取_随机范围", u8"返回区间内随机浮点数", g_randRange);
    regCap(L, ctx, u8"随机", u8"获取_随机整数", u8"返回区间内随机整数", g_randInt);
    regCap(L, ctx, u8"随机", u8"设置_随机种子", u8"设置随机数种子", g_randSeed);
    regCap(L, ctx, u8"脚本", u8"加载_脚本", u8"运行包内或磁盘的 Gvts 脚本文件：路径；成功返回真，失败返回 nil, 原因", g_script_include);
}
