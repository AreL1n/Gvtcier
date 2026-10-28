#include "Rhidriver/backend.h"
#include "Rhidriver/sg/glsl2spv.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"
#include "shader_ubo.gen.h"

#include <cstring>

namespace rhi {
namespace sg {

static int StageIndex(unsigned type) {
    switch (type) {
        case VertexShader:   return 0;
        case FragmentShader: return 1;
        case GeometryShader: return 2;
        case ComputeShader:  return 3;
        default:             return -1;
    }
}

Stage t_Gvtcier_Rhi_CreateShader(unsigned type) {
    Stage id = NewStage();
    StageObj* s = StageAt(id);
    if (s) {
        s->type = type;
        s->compiled = false;
    }
    return id;
}

void t_Gvtcier_Rhi_ShaderSource(Stage s, int count, const char* const* str, const int* len) {
    StageObj* o = StageAt(s);
    if (!o || !str) return;
    o->source.clear();
    for (int i = 0; i < count; ++i) {
        if (!str[i]) continue;
        if (len && len[i] > 0) o->source.append(str[i], (size_t)len[i]);
        else o->source.append(str[i]);
    }
}

void t_Gvtcier_Rhi_ShaderSourceBinary(Stage s, const void* data, int len) {
    StageObj* o = StageAt(s);
    if (!o || !data || len <= 0) return;
    o->spirv.assign((const uint32_t*)data, (const uint32_t*)((const uint8_t*)data + len));
}

// 送进来的是 GLSL 文本还是 SPIR-V 二进制都能吃：
// 二进制直接用，文本就现编。编译失败会把 glslang 的日志留下来，方便定位。
void t_Gvtcier_Rhi_CompileShader(Stage s) {
    StageObj* o = StageAt(s);
    if (!o) return;

    if (!o->source.empty()) {
        std::vector<uint32_t> spv;
        std::string log;
        if (!GlslToSpirv(o->type, o->source, spv, log)) {
            o->compiled = false;
            o->log = log;
            return;
        }
        o->spirv.swap(spv);
    }

    if (o->spirv.empty()) {
        o->compiled = false;
        o->log = "着色器既没有源码也没有字节码";
        return;
    }
    o->compiled = true;
}

void t_Gvtcier_Rhi_GetShaderiv(Stage s, unsigned pname, int* params) {
    StageObj* o = StageAt(s);
    if (!params) return;
    if (!o) { *params = 0; return; }
    if (pname == CompileStatus) *params = o->compiled ? 1 : 0;
    else if (pname == InfoLogLength) *params = (int)o->log.size() + 1;
    else *params = 0;
}

void t_Gvtcier_Rhi_GetShaderInfoLog(Stage s, int bufSize, int* len, char* log) {
    StageObj* o = StageAt(s);
    static const std::string kEmpty;
    const std::string& src = o ? o->log : kEmpty;
    int n = (int)src.size();
    if (len) *len = n;
    if (log && bufSize > 0) {
        int c = n < bufSize - 1 ? n : bufSize - 1;
        memcpy(log, src.data(), (size_t)c);
        log[c] = '\0';
    }
}

void t_Gvtcier_Rhi_DeleteShader(Stage s) {
    StageObj* o = StageAt(s);
    if (o) o->spirv.clear();
}

Program t_Gvtcier_Rhi_CreateProgram() {
    Program id = NewProgram();
    ProgramObj* p = ProgramAt(id);
    if (p) {
        // 缓冲区大小必须跟着色器里的 GvU 块一致，否则超出的成员会被 WriteUniform
        // 的越界检查直接丢掉（ASM 那 1.5KB 正好在 4096 之外）
        p->uniforms.resize((size_t)kGvUboSize);
        p->compiled = true;
    }
    return id;
}

void t_Gvtcier_Rhi_AttachShader(Program p, Stage s) {
    ProgramObj* prog = ProgramAt(p);
    StageObj* st = StageAt(s);
    if (!prog || !st) return;
    int idx = StageIndex(st->type);
    if (idx < 0) return;
    prog->stages[idx] = st;
}

void t_Gvtcier_Rhi_LinkProgram(Program p) {
    ProgramObj* prog = ProgramAt(p);
    if (!prog) return;
    prog->compiled = prog->stages[0] && prog->stages[0]->compiled;
}

void t_Gvtcier_Rhi_GetProgramiv(Program p, unsigned pname, int* params) {
    ProgramObj* prog = ProgramAt(p);
    if (!params) return;
    if (!prog) { *params = 0; return; }
    if (pname == LinkStatus) *params = prog->compiled ? 1 : 0;
    else if (pname == InfoLogLength) *params = 1;
    else *params = 0;
}

void t_Gvtcier_Rhi_GetProgramInfoLog(Program p, int bufSize, int* len, char* log) {
    (void)p;
    if (len) *len = 0;
    if (log && bufSize > 0) log[0] = '\0';
}

void t_Gvtcier_Rhi_DeleteProgram(Program p) {
    ProgramObj* prog = ProgramAt(p);
    if (!prog) return;
    if (prog->pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(prog->pipeline);
        prog->pipeline = sg_pipeline{};
    }
    if (prog->shader.id != SG_INVALID_ID) {
        sg_destroy_shader(prog->shader);
        prog->shader = sg_shader{};
    }
    for (int i = 0; i < 4; ++i) prog->stages[i] = nullptr;
    prog->compiled = false;
}

void t_Gvtcier_Rhi_UseProgram(Program p) { g_state.curProg = p; }

Location t_Gvtcier_Rhi_UniformLocation(Program p, const char* name) {
    ProgramObj* prog = ProgramAt(p);
    if (!prog || !name) return -1;
    auto it = prog->uniformOffset.find(name);
    if (it == prog->uniformOffset.end()) return -1;
    return (Location)it->second;
}

static void WriteUniform(ProgramObj* p, Location l, const void* data, size_t size) {
    if (!p || l < 0) return;
    size_t off = (size_t)l;
    if (off + size > p->uniforms.size()) return;
    memcpy(p->uniforms.data() + off, data, size);
}

void t_Gvtcier_Rhi_Uniform1i(Location l, int v) {
    ProgramObj* p = ProgramAt(g_state.curProg);
    WriteUniform(p, l, &v, 4);
    if (p && v >= 0 && v < 16 && l >= 0) p->samplerUnit[0] = v;
}

void t_Gvtcier_Rhi_Uniform1f(Location l, float v) { WriteUniform(ProgramAt(g_state.curProg), l, &v, 4); }

void t_Gvtcier_Rhi_Uniform2f(Location l, float x, float y) {
    float t[2] = {x, y};
    WriteUniform(ProgramAt(g_state.curProg), l, t, 8);
}

void t_Gvtcier_Rhi_Uniform3f(Location l, float x, float y, float z) {
    float t[3] = {x, y, z};
    WriteUniform(ProgramAt(g_state.curProg), l, t, 12);
}

void t_Gvtcier_Rhi_Uniform4f(Location l, float x, float y, float z, float w) {
    float t[4] = {x, y, z, w};
    WriteUniform(ProgramAt(g_state.curProg), l, t, 16);
}

void t_Gvtcier_Rhi_UniformMatrix4fv(Location l, int count, bool transpose, const float* v) {
    (void)transpose;
    WriteUniform(ProgramAt(g_state.curProg), l, v, (size_t)count * 64);
}

unsigned t_Gvtcier_Rhi_GetUniformBlockIndex(Program p, const char* name) {
    (void)p; (void)name;
    return InvalidIndex;
}

void t_Gvtcier_Rhi_UniformBlockBinding(Program p, unsigned blockIndex, unsigned binding) {
    (void)p; (void)blockIndex; (void)binding;
}

void t_Gvtcier_Rhi_GetActiveUniformBlockiv(Program p, unsigned blockIndex, unsigned pname, int* params) {
    (void)p; (void)blockIndex;
    if (params) *params = (pname == UniformBlockDataSize) ? 4096 : 0;
}

void t_Gvtcier_Rhi_GetUniformIndices(Program p, int count, const char* const* names, unsigned* indices) {
    ProgramObj* prog = ProgramAt(p);
    for (int i = 0; i < count; ++i) {
        unsigned found = InvalidIndex;
        if (prog && names && names[i]) {
            auto it = prog->uniformOffset.find(names[i]);
            if (it != prog->uniformOffset.end()) found = (unsigned)it->second;
        }
        if (indices) indices[i] = found;
    }
}

void t_Gvtcier_Rhi_GetActiveUniformsiv(Program p, int count, const unsigned* indices, unsigned pname, int* params) {
    ProgramObj* prog = ProgramAt(p);
    if (!params) return;
    for (int i = 0; i < count; ++i) {
        int v = 0;
        if (prog && indices && indices[i] != InvalidIndex) {
            if (pname == UniformOffset) v = (int)indices[i];
            else if (pname == UniformArraySize) v = 1;
        }
        params[i] = v;
    }
}

void t_Gvtcier_Rhi_GetActiveUniformName(Program p, unsigned index, int bufSize, int* length, char* name) {
    (void)p; (void)index;
    if (length) *length = 0;
    if (name && bufSize > 0) name[0] = '\0';
}

void t_Gvtcier_Rhi_DispatchCompute(unsigned x, unsigned y, unsigned z) { (void)x; (void)y; (void)z; }
void t_Gvtcier_Rhi_MemoryBarrier(unsigned barriers) { (void)barriers; }
bool t_Gvtcier_Rhi_HasCompute() { return false; }
bool t_Gvtcier_Rhi_HasInstancing() { return true; }

}
}
