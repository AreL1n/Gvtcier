#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace rhi;

//从打包资源里按名字取数据（名字不区分大小写，由资源层内部大写化后查找）。
extern "C" const void* res_get(const char* name, int* outSize);

//着色器编译失败的守卫：一次启动里可能有几十个着色器，失败时逐个弹日志会刷屏，
//这里只累计次数并记住第一个失败者，供上层在启动末尾统一提示。
namespace shader_guard {

//累计失败次数。
static int g_failures = 0;
//第一个失败的着色器标记（如 "vertex"/"fragment"/"link"），最多保留 255 字节。
static char g_first[256] = {};

//清零统计，通常在准备批量编译之前调用。
void Gvtcier_Shader_ResetFailures() {
    g_failures = 0;
    g_first[0] = '\0';
}

//记录一次失败；tag 只用于日志与提示，调用方保证字符串存活。
void Gvtcier_Shader_NoteFailure(const char* tag) {
    ++g_failures;
    if (g_first[0] == '\0' && tag) {
        snprintf(g_first, sizeof(g_first), "%s", tag);
    }
}

//返回累计失败次数。
int Gvtcier_Shader_FailureCount() { return g_failures; }

//返回第一个失败的标记；没有失败时返回空串。
const char* Gvtcier_Shader_FirstFailure() { return g_first; }

}

//展开 GLSL 的 #include 指令：逐行扫描，命中 #include "名字" 时把对应资源的内容原地插入。
//只支持双引号形式且必须顶格（允许前置空格/制表符），够用且不需要真正的预处理器。
//gvu.glsl 是公共 uniform 块，被打包成固定 id 34 的着色器资源，所以走 res_get_shader 而不是按名字找。
static std::vector<char> expandIncludes(const char* src, int len) {
    std::vector<char> out;
    if (!src || len <= 0) return out;
    const char* p = src;
    const char* end = src + len;
    while (p < end) {
        const char* nl = p;
        while (nl < end && *nl != '\n') ++nl;
        const char* next = (nl < end) ? nl + 1 : end;
        const char* q = p;
        while (q < nl && (*q == ' ' || *q == '\t')) ++q;
        if (nl - q > 10 && memcmp(q, "#include \"", 10) == 0) {
            const char* e = q + 10;
            while (e < nl && *e != '"') ++e;
            std::string name(q + 10, (size_t)(e - q - 10));
            int sz = 0;
            const char* data = nullptr;
            if (name == "gvu.glsl") data = (const char*)res_get_shader(34, &sz);
            else data = (const char*)res_get(name.c_str(), &sz);
            if (data && sz > 0) {
                out.insert(out.end(), data, data + sz);
                if (out.back() != '\n') out.push_back('\n');
                p = next;
                continue;
            }
        }
        out.insert(out.end(), p, next);
        p = next;
    }
    return out;
}

//为 GLES 后端重写着色器头部：源码里写的是桌面版 #version，这里换成 GLES 3.2 的 #version 320 es。
//必须显式声明 precision 限定符，GLES 的片元着色器默认没有 float 精度，不写会编译报错；
//同时定义 GV_ES 宏，让同一份源码有条件地走移动端分支。
//stage 区分顶点与片元：片元额外声明采样器精度，顶点着色器不需要。
static std::vector<char> glesShaderSource(const char* src, int len, unsigned stage) {
    std::vector<char> out;
    if (!src || len <= 0) return out;
    const bool isFrag = (stage == FragmentShader);
    const char* header = isFrag
        ? "#version 320 es\n"
          "#define GV_ES 1\n"
          "precision highp float;\n"
          "precision highp int;\n"
          "precision highp sampler2D;\n"
          "precision highp sampler3D;\n"
          "precision highp sampler2DArray;\n"
          "precision highp samplerCube;\n"
          "precision highp sampler2DShadow;\n"
        : "#version 320 es\n"
          "#define GV_ES 1\n"
          "precision highp float;\n"
          "precision highp int;\n";
    const char* p = src;
    const char* end = src + len;
    if (len > 8 && memcmp(p, "#version", 8) == 0) {
        const char* nl = p;
        while (nl < end && *nl != '\n') ++nl;
        if (nl < end) ++nl;
        out.insert(out.end(), header, header + strlen(header));
        out.insert(out.end(), nl, end);
    } else {
        out.insert(out.end(), header, header + strlen(header));
        out.insert(out.end(), p, end);
    }
    out.push_back('\0');
    return out;
}

//桌面 OpenGL 后端只需在 #version 行之后追加 #define GV_ES 0，
//保留原 #version 不动（桌面 GL 的版本声明必须位于最前），供源码里同一套宏分支使用。
static std::vector<char> desktopShaderSource(const char* src, int len) {
    std::vector<char> out;
    if (!src || len <= 0) return out;
    const char* header = "#define GV_ES 0\n";
    const char* p = src;
    const char* end = src + len;
    if (len > 8 && memcmp(p, "#version", 8) == 0) {
        const char* nl = p;
        while (nl < end && *nl != '\n') ++nl;
        if (nl < end) ++nl;
        out.insert(out.end(), p, nl);
        out.insert(out.end(), header, header + strlen(header));
        out.insert(out.end(), nl, end);
    } else {
        out.insert(out.end(), header, header + strlen(header));
        out.insert(out.end(), p, end);
    }
    out.push_back('\0');
    return out;
}

//编译单个着色器阶段并返回着色器对象；失败返回 0。
//Vulkan 后端现在是运行时用 glslang 把 GLSL 编成 SPIR-V，所以同样要展开 #include，
//只是不改版本头。OpenGL/GLES 还是交给驱动编译源码。
//失败时会取驱动或 glslang 的日志，并记入 shader_guard 统计。
static unsigned int compileShader(unsigned type, const char* src, int len, const char* tag) {
    const bool isVk = (rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Vulkan);
    const char* finalSrc = src;
    int finalLen = len;
    std::vector<char> adjusted;
    std::vector<char> expanded;
    if (src && len > 0) {
        expanded = expandIncludes(src, len);
        if (!expanded.empty()) {
            expanded.push_back('\0');
            src = expanded.data();
            len = (int)expanded.size() - 1;
            finalSrc = src;
            finalLen = len;
        }
        if (rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Gles) {
            //GLES 后端：换版本头 + 补精度声明。
            adjusted = glesShaderSource(src, len, type);
        } else if (!isVk) {
            //桌面 OpenGL 后端：仅追加宏定义。VK 那边是 glslang 现编，不用改头。
            adjusted = desktopShaderSource(src, len);
        }
        if (!adjusted.empty()) {
            finalSrc = adjusted.data();
            finalLen = (int)adjusted.size() - 1;
        }
    }
    unsigned int s = rhi::Gvtcier_Rhi_CreateShader(type);
    rhi::Gvtcier_Rhi_ShaderSource(s, 1, &finalSrc, &finalLen);
    rhi::Gvtcier_Rhi_CompileShader(s);
    int ok = 0;
    rhi::Gvtcier_Rhi_GetShaderiv(s, CompileStatus, &ok);
    if (!ok) {
        //编译失败：取 InfoLog。日志里带行号，配合展开后的源码能直接定位到出错位置。
        int logLen = 0;
        rhi::Gvtcier_Rhi_GetShaderiv(s, InfoLogLength, &logLen);
        std::vector<char> log((size_t)logLen + 1);
        rhi::Gvtcier_Rhi_GetShaderInfoLog(s, logLen, nullptr, log.data());
        //以"阶段:日志内容"为键做去重打印，同一个错误在多个着色器里重复出现时只报一次。
        char key[128];
        snprintf(key, sizeof(key), "scomp:%s:%s", tag, log.data());
        rlog::once(key, "[shader] %s compile error:\n%s\n", tag, log.data());
        shader_guard::Gvtcier_Shader_NoteFailure(tag);
        //失败的对象必须显式删除，否则会泄漏驱动侧的着色器资源。
        rhi::Gvtcier_Rhi_DeleteShader(s);
        return 0;
    }
    return s;
}

//销毁程序对象并清空 uniform 位置缓存。
//缓存必须一起清，否则同一个 Shader 实例重新加载后会用上一份程序的旧位置去设置 uniform。
void Shader::Gvtcier_Shader_Destroy() {
    locCache_.clear();
    if (id_) {
        rhi::Gvtcier_Rhi_DeleteProgram(id_);
        id_ = 0;
    }
}

//查询并缓存 uniform 位置。
//名字先按原样查；查不到再补上 "GvU." 前缀重试，因为公共 uniform 块里的变量全名带该前缀，
//而调用方通常只写短名。GvU. 开头的名字不重复加前缀。查不到也会被缓存，避免每帧重复查询。
rhi::Location Shader::locOf(const char* name) const {
    auto it = locCache_.find(name);
    if (it != locCache_.end()) return it->second;
    rhi::Location loc = rhi::Gvtcier_Rhi_UniformLocation(id_, name);
    if (loc < 0 && std::strncmp(name, "GvU.", 4) != 0) {
        char full[192];
        snprintf(full, sizeof(full), "GvU.%s", name);
        loc = rhi::Gvtcier_Rhi_UniformLocation(id_, full);
    }
    locCache_.emplace(name, loc);
    return loc;
}

//从内存源码加载一对顶点/片元着色器并链接成程序。
//先销毁旧程序，保证重复加载不会泄漏；任一阶段编译失败即整体失败，已编译成功的阶段由驱动在程序销毁时回收。
//链接成功后立即删除着色器对象（程序已持有引用），减少驱动侧资源占用。
//返回 true 表示程序可用；false 时 id_ 为 0。
bool Shader::Gvtcier_Shader_LoadFromMemory(const char* vertSrc, int vertLen, const char* fragSrc, int fragLen) {
    Gvtcier_Shader_Destroy();
    unsigned int vsObj = compileShader(VertexShader, vertSrc, vertLen, "vertex");
    unsigned int fsObj = compileShader(FragmentShader, fragSrc, fragLen, "fragment");
    if (!vsObj || !fsObj) return false;

    id_ = rhi::Gvtcier_Rhi_CreateProgram();
    rhi::Gvtcier_Rhi_AttachShader(id_, vsObj);
    rhi::Gvtcier_Rhi_AttachShader(id_, fsObj);
    rhi::Gvtcier_Rhi_LinkProgram(id_);
    rhi::Gvtcier_Rhi_DeleteShader(vsObj);
    rhi::Gvtcier_Rhi_DeleteShader(fsObj);

    int ok = 0;
    rhi::Gvtcier_Rhi_GetProgramiv(id_, LinkStatus, &ok);
    if (!ok) {
        //链接失败常见于阶段之间 varying 类型或名字不匹配，与编译失败分开记账，便于区分问题来源。
        int len = 0;
        rhi::Gvtcier_Rhi_GetProgramiv(id_, InfoLogLength, &len);
        std::vector<char> log((size_t)len + 1);
        rhi::Gvtcier_Rhi_GetProgramInfoLog(id_, len, nullptr, log.data());
        char key[128];
        snprintf(key, sizeof(key), "slink:%s", log.data());
        rlog::once(key, "[shader] link error:\n%s\n", log.data());
        shader_guard::Gvtcier_Shader_NoteFailure("link");
        rhi::Gvtcier_Rhi_DeleteProgram(id_);
        id_ = 0;
        return false;
    }
    return true;
}

//加载计算着色器（只有计算阶段，没有顶点/片元配对）。流程与图形管线一致：编译 → 链接 → 检查 → 删阶段对象。
//返回 true 表示程序可用；false 时 id_ 为 0。
bool Shader::Gvtcier_Shader_LoadCompute(const char* src, int len) {
    Gvtcier_Shader_Destroy();
    unsigned int cs = compileShader(ComputeShader, src, len, "compute");
    if (!cs) return false;
    id_ = rhi::Gvtcier_Rhi_CreateProgram();
    rhi::Gvtcier_Rhi_AttachShader(id_, cs);
    rhi::Gvtcier_Rhi_LinkProgram(id_);
    rhi::Gvtcier_Rhi_DeleteShader(cs);
    int ok = 0;
    rhi::Gvtcier_Rhi_GetProgramiv(id_, LinkStatus, &ok);
    if (!ok) {
        int l = 0;
        rhi::Gvtcier_Rhi_GetProgramiv(id_, InfoLogLength, &l);
        std::vector<char> log((size_t)l + 1);
        rhi::Gvtcier_Rhi_GetProgramInfoLog(id_, l, nullptr, log.data());
        char key[128];
        snprintf(key, sizeof(key), "sclink:%s", log.data());
        rlog::once(key, "[shader] compute link error:\n%s\n", log.data());
        shader_guard::Gvtcier_Shader_NoteFailure("compute link");
        rhi::Gvtcier_Rhi_DeleteProgram(id_);
        id_ = 0;
        return false;
    }
    return true;
}

//以下一组 uniform 设置函数都会先 UseProgram。
//这是为了防错：uniform 上传作用于"当前程序"，调用方可能刚切换过别的着色器，
//在这里统一切回自己再设置，保证值一定写进本程序的 uniform。
void Shader::Gvtcier_Shader_SetInt(const char* name, int v) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_Uniform1i(locOf(name), v);
}

void Shader::Gvtcier_Shader_SetFloat(const char* name, float v) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_Uniform1f(locOf(name), v);
}

void Shader::Gvtcier_Shader_SetVec2(const char* name, float x, float y) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_Uniform2f(locOf(name), x, y);
}

void Shader::Gvtcier_Shader_SetVec3(const char* name, float x, float y, float z) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_Uniform3f(locOf(name), x, y, z);
}

void Shader::Gvtcier_Shader_SetVec4(const char* name, float x, float y, float z, float w) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_Uniform4f(locOf(name), x, y, z, w);
}

void Shader::Gvtcier_Shader_SetMat4(const char* name, const float* m) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_UniformMatrix4fv(locOf(name), 1, false, m);
}

//double 版矩阵设置：图形接口只接受 float，这里先逐元素转换。
//引擎内部计算用 double 保证精度，上传时降为 float 是精度与带宽的折中。
void Shader::Gvtcier_Shader_SetMat4(const char* name, const double* m) const {
    float f[16];
    for (int i = 0; i < 16; ++i) f[i] = (float)m[i];
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_UniformMatrix4fv(locOf(name), 1, false, f);
}

//批量上传 count 个连续的 mat4，用于骨骼矩阵、级联阴影矩阵等等长数组。
void Shader::Gvtcier_Shader_SetMat4Array(const char* name, const float* m, int count) const {
    rhi::Gvtcier_Rhi_UseProgram(id_);
    rhi::Gvtcier_Rhi_UniformMatrix4fv(locOf(name), count, false, m);
}
