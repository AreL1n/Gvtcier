#include "Gvtcier.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cctype>

namespace GvtcierError {

//把错误大类翻译成中文，用于统一错误消息的前缀。
const char* typeName(Type t) {
    switch (t) {
        case Type::Syntax:   return u8"语法错误";
        case Type::Lexical:  return u8"词法错误";
        case Type::Argument: return u8"参数错误";
        case Type::Runtime:  return u8"运行错误";
        case Type::Engine:   return u8"引擎错误";
        case Type::Gvts:      return u8"Gvts错误";
        case Type::Language: return u8"语言错误";
        default:             return u8"错误";
    }
}

//拼装统一格式的错误文本：<文件> 行行号 可选{源码片段} 类型: 消息。
//带上源码片段是为了让玩家/开发者一眼看出是脚本哪一行出的问题；没有片段时格式自动缩短。
std::string format(const char* file, int line, const char* source, Type t, const std::string& msg) {
    char buf[512];
    if (source && source[0]) {
        snprintf(buf, sizeof buf, u8"<%s> 行%d {%s} %s: %s",
                 file ? file : "?", line, source, typeName(t), msg.c_str());
    } else {
        snprintf(buf, sizeof buf, u8"<%s> 行%d %s: %s",
                 file ? file : "?", line, typeName(t), msg.c_str());
    }
    return buf;
}

//把脚本解释器抛出的英文错误信息替换成中文。
//脚本运行时直接暴露英文原文对玩家不友好，这里对常见的运行时/语法错误做整串替换。
//只做子串替换而非逐句翻译：解释器消息格式固定，匹配到关键短语就足以让玩家看懂；
//未匹配的部分保持英文，避免错误翻译制造更多误解。
std::string translateGvts(const char* msg) {
    if (!msg) return u8"未知错误";
    std::string s(msg);
    static const struct { const char* en; const char* zh; } map[] = {
        { "attempt to index a nil value", u8"尝试索引空值nil" },
        { "attempt to call a nil value", u8"尝试调用空值nil" },
        { "attempt to call a non-function", u8"尝试调用非函数值" },
        { "attempt to perform arithmetic on a nil value", u8"对空值nil执行算术运算" },
        { "attempt to perform arithmetic on a string value", u8"对字符串值执行算术运算" },
        { "attempt to concatenate a nil value", u8"对空值nil执行字符串拼接" },
        { "attempt to compare nil with", u8"尝试将nil与其他值比较" },
        { "attempt to get length of a nil value", u8"尝试获取空值nil的长度" },
        { "table index is nil", u8"表索引为nil" },
        { "invalid key to", u8"无效的键" },
        { "syntax error", u8"语法错误" },
        { "unexpected symbol", u8"意外的符号" },
        { "malformed number", u8"数字格式错误" },
        { "unfinished string", u8"字符串未闭合" },
        { "unfinished long string", u8"长字符串未闭合" },
        { "unfinished long comment", u8"长注释未闭合" },
        { "unknown variable", u8"未知变量" },
        { "unknown global", u8"未知全局变量" },
        { "name conflict for global", u8"全局名称冲突" },
        { "stack overflow", u8"栈溢出" },
        { "not enough memory", u8"内存不足" },
        { "bad argument", u8"参数错误" },
        { "number expected", u8"期望数字" },
        { "string expected", u8"期望字符串" },
        { "function expected", u8"期望函数" },
        { "table expected", u8"期望表" },
        { "boolean expected", u8"期望布尔值" },
        { "integer expected", u8"期望整数" },
        { "no value", u8"无值" },
        { "value has no length", u8"值没有长度" },
        { "cannot close non-closable value", u8"无法关闭不可关闭的值" },
    };
    for (auto& e : map) {
        //同一份消息里可能命中多个短语，循环全部替换而不是命中即返回。
        size_t p = s.find(e.en);
        if (p != std::string::npos) s.replace(p, strlen(e.en), e.zh);
    }
    return s;
}

//检查脚本文本里是否用了英文关键字，返回一条提示（没有问题时返回空串）。
//本引擎支持中文关键字（如 函数/如果/那么），但许多玩家会照其它语言的习惯写英文，
//所以这里做非侵入式的提醒而不是报错。
//扫描时跳过字符串字面量、单行注释 // 与 -- 、长注释 --[[ ]] 和长字符串 [[ ]]，避免把内容当成代码误报。
std::string checkLanguage(const char* msg) {
    if (!msg) return std::string();
    static const struct { const char* en; const char* zh; } kw[] = {
        { "elseif", u8"否则如果" },
        { "function", u8"函数" },
        { "repeat", u8"重复" },
        { "return", u8"返回" },
        { "until", u8"直到" },
        { "while", u8"当" },
        { "break", u8"跳出" },
        { "false", u8"假" },
        { "local", u8"局部" },
        { "then", u8"那么" },
        { "true", u8"真" },
        { "else", u8"否则" },
        { "goto", u8"跳转" },
        { "and", u8"且" },
        { "end", u8"}" },
        { "for", u8"循环" },
        { "nil", u8"空" },
        { "not", u8"非" },
        { "in", u8"在" },
        { "or", u8"或" },
    };
    const char* p = msg;
    while (*p) {
        char c = *p;
        if (c == '"' || c == '\'') {
            char q = c;
            ++p;
            while (*p && *p != q) {
                if (*p == '\\' && p[1]) p += 2;
                else ++p;
            }
            if (*p) ++p;
            continue;
        }
        if (c == '/' && p[1] == '/') {
            while (*p && *p != '\n') ++p;
            continue;
        }
        if (c == '-' && p[1] == '-') {
            if (p[2] == '[' && p[3] == '[') {
                p += 4;
                while (*p && !(p[0] == ']' && p[1] == ']')) ++p;
                if (*p) p += 2;
                continue;
            }
            while (*p && *p != '\n') ++p;
            continue;
        }
        if (c == '[' && p[1] == '[') {
            //长字符串：整段跳过，内部的词不参与关键字检查。
            p += 2;
            while (*p && !(p[0] == ']' && p[1] == ']')) ++p;
            if (*p) p += 2;
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            //按标识符边界取出一个完整单词，再和英文关键字表逐字比较，避免 "endpoint" 命中 "end"。
            const char* w = p;
            while (isalnum((unsigned char)*p) || *p == '_') ++p;
            size_t len = (size_t)(p - w);
            for (auto& e : kw) {
                if (strlen(e.en) == len && strncmp(w, e.en, len) == 0) {
                    char buf[256];
                    snprintf(buf, sizeof buf,
                             u8"语言警告 <%.*s> 为英文关键字，本引擎对应 <%s>，建议一并改成中文",
                             (int)len, w, e.zh);
                    return std::string(buf);
                }
            }
            continue;
        }
        ++p;
    }
    return std::string();
}

//语言检查的便利入口：有提示就打印到控制台。不阻断执行，纯提醒。
void warnLanguage(const char* msg) {
    std::string w = checkLanguage(msg);
    if (!w.empty()) printf("[GvtcierGvts][警告] %s\n", w.c_str());
}

//引擎级错误报告：打上 [GvtcierEngine][tag] 前缀输出到控制台，同时写入日志文件。
//tag 用于归类问题来源（如 window/renderer/physics），fmt 与 printf 一致。
//副作用：立即刷新 stdout，保证崩溃前也能看到这条错误。
void engine(const char* tag, const char* fmt, ...) {
    printf("[GvtcierEngine][%s] ", tag);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    rlog::Gvtcier_Log_Write("[GvtcierEngine][%s] %s\n", tag, buf);
}

//脚本错误的外部接收者（例如调试器或脚本层），可为空。
static GvtsErrorSink g_errSink = nullptr;
//最近一次脚本错误的完整中文消息，供界面或调试器事后读取。
static char g_lastMsg[1024] = "";
//最近一次脚本错误所定位到的脚本文件路径（从消息里解析出来的 "xxx.gvts" 片段）。
static char g_lastFile[1024] = "";
//最近一次脚本错误所在行号；解析不出来时为 0。
static int g_lastLine = 0;
//是否发生过未被清除的脚本错误。
static bool g_hasError = false;

//注册脚本错误接收者；传空指针即注销。接收者由脚本层决定，引擎不持有其生命周期。
void setGvtsErrorSink(GvtsErrorSink fn) {
    g_errSink = fn;
}

//报告一个脚本错误。rawMsg 为解释器原始消息（英文），这里先翻译成中文，再尝试从中解析出文件与行号，
//然后存入最近错误快照、写日志，并转发给已注册的接收者。
//解析依赖消息里出现的 "gvts:行号" 或 ".gvts:行号" 形式：从冒号往前找不含空白/方括号的片段作为文件名。
void gvtsError(const char* rawMsg) {
    if (!rawMsg) rawMsg = u8"未知错误";
    std::string s = translateGvts(rawMsg);
    g_lastFile[0] = 0;
    g_lastLine = 0;
    const char* p = strstr(s.c_str(), "gvts:");
    if (!p) p = strstr(s.c_str(), ".gvts:");
    if (!p) p = strstr(s.c_str(), ".gvts:");
    if (p) {
        const char* ns = p;
        while (ns > s.c_str() && ns[-1] != ' ' && ns[-1] != '[' && ns[-1] != '\t') --ns;
        const char* colon = strchr(p, ':');
        if (colon) {
            size_t flen = (size_t)(colon - ns);
            if (flen > 0 && flen < sizeof(g_lastFile)) {
                memcpy(g_lastFile, ns, flen);
                g_lastFile[flen] = 0;
                g_lastLine = atoi(colon + 1);
            }
        }
    }
    snprintf(g_lastMsg, sizeof(g_lastMsg), "%s", s.c_str());
    g_hasError = true;
    rlog::Gvtcier_Log_Write("[脚本错误] %s", g_lastMsg);
    if (g_errSink) g_errSink(g_lastMsg, g_lastFile, g_lastLine);
}

//取最近一次脚本错误的内容、文件与行号。没有任何错误时返回 false 且不写输出参数。
//参数都允许为空指针，便于调用方只关心其中一项。缓冲区容量不足时按 C 风格截断。
bool lastGvtsError(char* outMsg, int msgCap, char* outFile, int fileCap, int* outLine) {
    if (!g_hasError) return false;
    if (outMsg && msgCap > 0) snprintf(outMsg, (size_t)msgCap, "%s", g_lastMsg);
    if (outFile && fileCap > 0) snprintf(outFile, (size_t)fileCap, "%s", g_lastFile);
    if (outLine) *outLine = g_lastLine;
    return true;
}

//清除脚本错误状态。脚本层在开始新一轮执行或玩家确认错误后调用，避免旧错误一直被判为未处理。
void clearGvtsError() {
    g_hasError = false;
    g_lastMsg[0] = 0;
    g_lastFile[0] = 0;
    g_lastLine = 0;
}

}
