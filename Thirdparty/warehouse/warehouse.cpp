#include "warehouse.h"

#include "Public/ResourceLimits.h"
#include "Public/ShaderLang.h"
#include "GlslangToSpv.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

EShLanguage ToStage(int stage) {
    switch (stage) {
        case 0: return EShLangVertex;
        case 3: return EShLangGeometry;
        case 4: return EShLangFragment;
        case 5: return EShLangCompute;
        default: return EShLangVertex;
    }
}

// Vulkan 比 OpenGL 严：UBO 必须显式 binding，接口变量必须显式 location。
// 引擎里的着色器是按 OpenGL 的宽松习惯写的，两边共用一份源码，
// 所以在送进 glslang 之前把缺的补齐，源码本身不改。
struct FixState {
    bool isVertex = false;
    bool isFragment = false;
    int ioLocation = 0;
    int bindingLocation = 0;
    int samplerBinding = 0;
    bool inBlock = false;
};

bool IsIdentStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool IsIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// 找出这一行开头是不是 in/out 声明。in 前面可能还挂着 flat / smooth / noperspective
// 这类插值限定符，所以要把前缀词剥掉再比。返回空串表示不是接口声明
std::string IoDirection(const std::string& line) {
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        size_t b = i;
        while (i < line.size() && IsIdentChar(line[i])) ++i;
        std::string w = line.substr(b, i - b);
        if (w.empty()) return "";
        if (w == "in" || w == "out") return w;
        // 这些是能出现在 in/out 前面的限定符，其余一律认为不是本行要处理的东西
        if (w == "flat" || w == "smooth" || w == "noperspective" ||
            w == "centroid" || w == "sample" || w == "patch") {
            continue;
        }
        return "";
    }
    return "";
}

// 一行里出现 uniform/buffer 作为声明关键字时返回它，否则返回空。
// 前面可能挂着 layout(...)，所以不能只看行首那个词
std::string BlockKeyword(const std::string& line) {
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
        if (i >= line.size() || line[i] == '#' || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')) return "";
        size_t b = i;
        while (i < line.size() && IsIdentChar(line[i])) ++i;
        std::string w = line.substr(b, i - b);
        if (w.empty()) return "";
        if (w == "uniform" || w == "buffer") return w;
        if (w == "layout" || w == "coherent" || w == "readonly" || w == "writeonly" || w == "restrict") {
            // 跳过 layout(...) 的括号内容
            if (w == "layout") {
                size_t lp = line.find('(', i);
                if (lp == std::string::npos) return "";
                size_t rp = line.find(')', lp);
                if (rp == std::string::npos) return "";
                i = rp + 1;
            }
            continue;
        }
        return "";
    }
    return "";
}

// 找出这一行里第一个"实义" token，遇到注释或预处理指令就跳过
std::string FirstWord(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && std::isspace((unsigned char)line[i])) ++i;
    if (i >= line.size() || line[i] == '#' || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')) return "";
    size_t b = i;
    while (i < line.size() && IsIdentChar(line[i])) ++i;
    return line.substr(b, i - b);
}

std::string PatchGlsl(const std::string& src, FixState st) {
    // Vulkan 的顶点序号内建变量叫 gl_VertexIndex，GL 里叫 gl_VertexID。
    // 源码是按 GL 写的，转 SPIR-V 之前统一换成 Vulkan 的名字
    std::string body = src;
    {
        const std::string from = "gl_VertexID";
        const std::string to = "gl_VertexIndex";
        size_t pos = 0;
        while ((pos = body.find(from, pos)) != std::string::npos) {
            body.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string out;
    out.reserve(body.size() + 512);

    const char* p = body.c_str();
    const char* end = p + body.size();
    bool blockComment = false;

    while (p < end) {
        const char* nl = p;
        while (nl < end && *nl != '\n') ++nl;
        std::string line(p, (size_t)(nl - p));
        p = (nl < end) ? nl + 1 : end;

        std::string word = FirstWord(line);
        std::string trimmed = line;
        size_t lead = 0;
        while (lead < trimmed.size() && std::isspace((unsigned char)trimmed[lead])) ++lead;

        const bool hasLayout = trimmed.find("layout") != std::string::npos;
        const bool hasBinding = trimmed.find("binding") != std::string::npos;
        const bool hasLocation = trimmed.find("location") != std::string::npos;

        const std::string blockKw = blockComment ? std::string() : BlockKeyword(trimmed);

        // uniform / buffer 块：补 binding。源码里往往只写了 layout(std140)，
        // 缺的正是 binding 这一项，所以判据只看 binding
        if (!blockComment && !blockKw.empty() && !hasBinding &&
            trimmed.find('{') != std::string::npos) {
            const bool isUniform = (blockKw == "uniform");
            std::string head = trimmed.substr(0, lead);
            if (hasLayout) {
                // 已经有 layout(...)，往里塞 binding 就行，别套第二层 layout
                size_t lp = trimmed.find("layout");
                size_t lb = trimmed.find('(', lp);
                size_t rb = trimmed.find(')', lb);
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string inner = trimmed.substr(lb + 1, rb - lb - 1);
                    bool empty = inner.find_first_not_of(" \t") == std::string::npos;
                    std::string merged = "layout(" + inner + (empty ? "" : ", ") +
                                         "binding = " + std::to_string(st.bindingLocation) + ")";
                    out += head;
                    out += merged;
                    out += trimmed.substr(rb + 1);
                    out += "\n";
                    st.bindingLocation++;
                    continue;
                }
            }
            out += head;
            if (isUniform) {
                out += "layout(std140, binding = " + std::to_string(st.bindingLocation) + ") ";
            } else {
                out += "layout(std430, binding = " + std::to_string(st.bindingLocation) + ") ";
            }
            out += trimmed.substr(lead);
            out += "\n";
            st.bindingLocation++;
            continue;
        }

        // 接口变量：补 location。顶点 out 与片元 in 按同一套序号排，两边才能对上
        const std::string ioDir = blockComment ? std::string() : IoDirection(trimmed);
        const bool isIo = !ioDir.empty() && !hasLocation && trimmed.find('{') == std::string::npos;
        if (!blockComment && isIo) {
            const bool needPatch = (ioDir == "out" && (st.isVertex || st.isFragment)) ||
                                   (ioDir == "in" && st.isFragment);
            if (needPatch) {
                out += trimmed.substr(0, lead);
                out += "layout(location = " + std::to_string(st.ioLocation) + ") ";
                out += trimmed.substr(lead);
                out += "\n";
                st.ioLocation++;
                continue;
            }
        }

        // 采样器/贴图也要 binding。它跟 UBO 在描述符集里是分开的编号空间，
        // 所以单独占一套序号，从 0 起排
        if (!blockComment && word == "uniform" && !hasBinding && trimmed.find(';') != std::string::npos &&
            trimmed.find('{') == std::string::npos) {
            out += trimmed.substr(0, lead);
            out += "layout(binding = " + std::to_string(st.samplerBinding++) + ") ";
            out += trimmed.substr(lead);
            out += "\n";
            continue;
        }

        // 逐字符跟踪块注释状态，避免把注释里的 in/out 当成真声明
        for (size_t i = 0; i < line.size(); ++i) {
            if (blockComment) {
                if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                    blockComment = false;
                    ++i;
                }
            } else if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                blockComment = true;
                ++i;
            } else if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
                break;
            }
        }

        out += line;
        out += "\n";
    }
    return out;
}

TBuiltInResource MakeResource() {
    TBuiltInResource r;
    std::memset(&r, 0, sizeof(r));
    r.maxLights = 32;
    r.maxClipPlanes = 6;
    r.maxTextureUnits = 32;
    r.maxTextureCoords = 32;
    r.maxVertexAttribs = 64;
    r.maxVertexUniformComponents = 4096;
    r.maxVaryingFloats = 64;
    r.maxVertexTextureImageUnits = 32;
    r.maxCombinedTextureImageUnits = 80;
    r.maxTextureImageUnits = 32;
    r.maxFragmentUniformComponents = 4096;
    r.maxDrawBuffers = 32;
    r.maxVertexUniformVectors = 128;
    r.maxVaryingVectors = 8;
    r.maxFragmentUniformVectors = 16;
    r.maxVertexOutputVectors = 16;
    r.maxFragmentInputVectors = 15;
    r.minProgramTexelOffset = -8;
    r.maxProgramTexelOffset = 7;
    r.maxClipDistances = 8;
    r.maxComputeWorkGroupCountX = 65535;
    r.maxComputeWorkGroupCountY = 65535;
    r.maxComputeWorkGroupCountZ = 65535;
    r.maxComputeWorkGroupSizeX = 1024;
    r.maxComputeWorkGroupSizeY = 1024;
    r.maxComputeWorkGroupSizeZ = 64;
    r.maxComputeUniformComponents = 1024;
    r.maxComputeTextureImageUnits = 16;
    r.maxComputeImageUniforms = 8;
    r.maxComputeAtomicCounters = 8;
    r.maxComputeAtomicCounterBuffers = 1;
    r.maxVaryingComponents = 60;
    r.maxVertexOutputComponents = 64;
    r.maxGeometryInputComponents = 64;
    r.maxGeometryOutputComponents = 128;
    r.maxFragmentInputComponents = 128;
    r.maxImageUnits = 8;
    r.maxCombinedImageUnitsAndFragmentOutputs = 8;
    r.maxCombinedShaderOutputResources = 8;
    r.maxImageSamples = 0;
    r.maxVertexImageUniforms = 0;
    r.maxTessControlImageUniforms = 0;
    r.maxTessEvaluationImageUniforms = 0;
    r.maxGeometryImageUniforms = 0;
    r.maxFragmentImageUniforms = 8;
    r.maxCombinedImageUniforms = 8;
    r.maxGeometryTextureImageUnits = 16;
    r.maxGeometryOutputVertices = 256;
    r.maxGeometryTotalOutputComponents = 1024;
    r.maxGeometryUniformComponents = 1024;
    r.maxGeometryVaryingComponents = 64;
    r.maxTessControlInputComponents = 128;
    r.maxTessControlOutputComponents = 128;
    r.maxTessControlTextureImageUnits = 16;
    r.maxTessControlUniformComponents = 1024;
    r.maxTessControlTotalOutputComponents = 4096;
    r.maxTessEvaluationInputComponents = 128;
    r.maxTessEvaluationOutputComponents = 128;
    r.maxTessEvaluationTextureImageUnits = 16;
    r.maxTessEvaluationUniformComponents = 1024;
    r.maxTessPatchComponents = 120;
    r.maxPatchVertices = 32;
    r.maxTessGenLevel = 64;
    r.maxViewports = 16;
    r.maxVertexAtomicCounters = 0;
    r.maxTessControlAtomicCounters = 0;
    r.maxTessEvaluationAtomicCounters = 0;
    r.maxGeometryAtomicCounters = 0;
    r.maxFragmentAtomicCounters = 8;
    r.maxCombinedAtomicCounters = 8;
    r.maxAtomicCounterBindings = 1;
    r.maxVertexAtomicCounterBuffers = 0;
    r.maxTessControlAtomicCounterBuffers = 0;
    r.maxTessEvaluationAtomicCounterBuffers = 0;
    r.maxGeometryAtomicCounterBuffers = 0;
    r.maxFragmentAtomicCounterBuffers = 1;
    r.maxCombinedAtomicCounterBuffers = 1;
    r.maxAtomicCounterBufferSize = 16384;
    r.maxTransformFeedbackBuffers = 4;
    r.maxTransformFeedbackInterleavedComponents = 64;
    r.maxCullDistances = 8;
    r.maxCombinedClipAndCullDistances = 8;
    r.maxSamples = 4;
    r.maxMeshOutputVerticesNV = 256;
    r.maxMeshOutputPrimitivesNV = 512;
    r.maxMeshWorkGroupSizeX_NV = 32;
    r.maxMeshWorkGroupSizeY_NV = 1;
    r.maxMeshWorkGroupSizeZ_NV = 1;
    r.maxTaskWorkGroupSizeX_NV = 32;
    r.maxTaskWorkGroupSizeY_NV = 1;
    r.maxTaskWorkGroupSizeZ_NV = 1;
    r.maxMeshViewCountNV = 4;
    r.maxMeshOutputVerticesEXT = 256;
    r.maxMeshOutputPrimitivesEXT = 256;
    r.maxMeshWorkGroupSizeX_EXT = 128;
    r.maxMeshWorkGroupSizeY_EXT = 128;
    r.maxMeshWorkGroupSizeZ_EXT = 128;
    r.maxTaskWorkGroupSizeX_EXT = 128;
    r.maxTaskWorkGroupSizeY_EXT = 128;
    r.maxTaskWorkGroupSizeZ_EXT = 128;
    r.maxMeshViewCountEXT = 4;
    r.maxDualSourceDrawBuffersEXT = 1;

    r.limits.nonInductiveForLoops = 1;
    r.limits.whileLoops = 1;
    r.limits.doWhileLoops = 1;
    r.limits.generalUniformIndexing = 1;
    r.limits.generalAttributeMatrixVectorIndexing = 1;
    r.limits.generalVaryingIndexing = 1;
    r.limits.generalSamplerIndexing = 1;
    r.limits.generalVariableIndexing = 1;
    r.limits.generalConstantMatrixVectorIndexing = 1;
    return r;
}

std::string g_log;

void AppendLog(const char* s) {
    if (s && s[0]) g_log += s;
}

}

extern "C" {

WAREHOUSE_API int wh_init(void) {
    static bool done = false;
    if (done) return 1;
    done = glslang::InitializeProcess();
    return done ? 1 : 0;
}

WAREHOUSE_API void wh_shutdown(void) {
    glslang::FinalizeProcess();
}

WAREHOUSE_API int wh_compile_glsl(int stage, const char* source, int length,
                                  unsigned int** out_words, int* out_count, const char** out_log) {
    g_log.clear();
    if (out_words) *out_words = nullptr;
    if (out_count) *out_count = 0;

    if (!source || length <= 0) {
        g_log = "源码为空";
        if (out_log) *out_log = g_log.c_str();
        return 0;
    }

    std::string text(source, (size_t)length);

    FixState fix;
    fix.isVertex = (stage == 0);
    fix.isFragment = (stage == 4);
    std::string patched = PatchGlsl(text, fix);
    const char* src = patched.c_str();

    const EShLanguage lang = ToStage(stage);
    glslang::TShader shader(lang);

    const int vkVersion = 100;
    shader.setStrings(&src, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, vkVersion);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    TBuiltInResource res = MakeResource();
    const EShMessages msg = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(&res, 460, false, msg)) {
        AppendLog(shader.getInfoLog());
        AppendLog(shader.getInfoDebugLog());
        if (out_log) *out_log = g_log.c_str();
        return 0;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(msg)) {
        AppendLog(program.getInfoLog());
        AppendLog(program.getInfoDebugLog());
        if (out_log) *out_log = g_log.c_str();
        return 0;
    }

    std::vector<unsigned int> spirv;
    glslang::SpvOptions opts;
    opts.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(lang), spirv, &opts);

    if (spirv.empty()) {
        g_log = "SPIR-V 结果为空";
        if (out_log) *out_log = g_log.c_str();
        return 0;
    }

    const size_t bytes = spirv.size() * sizeof(unsigned int);
    unsigned int* buf = (unsigned int*)::operator new(bytes);
    std::memcpy(buf, spirv.data(), bytes);
    if (out_words) *out_words = buf;
    if (out_count) *out_count = (int)spirv.size();
    if (out_log) *out_log = g_log.c_str();
    return 1;
}

WAREHOUSE_API void wh_free(void* p) {
    ::operator delete(p);
}

}
