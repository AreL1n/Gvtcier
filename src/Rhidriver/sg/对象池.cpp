#include "Rhidriver/sg/rhi_sg.h"
#include "bicu/core/log.h"

#include <cstdarg>
#include <cstdio>

namespace rhi {
namespace sg {

BindState g_state;

// 句柄从 1 开始，0 一律当空对象，跟 GL 的习惯对齐
template <typename T> struct Pool {
    std::vector<T> items;
    unsigned create() {
        items.emplace_back();
        return (unsigned)items.size();
    }
    T* at(unsigned id) {
        if (id == 0 || id > items.size()) return nullptr;
        return &items[id - 1];
    }
};

static Pool<BufferObj> g_buffers;
static Pool<ImageObj> g_images;
static Pool<SamplerObj> g_samplers;
static Pool<VaoObj> g_vaos;
static Pool<StageObj> g_stages;
static Pool<ProgramObj> g_programs;
static Pool<PassObj> g_passes;

Buffer NewBuffer() { return g_buffers.create(); }
Texture NewImage() {
    unsigned id = g_images.create();
    g_samplers.create();
    return id;
}
Vao NewVao() { return g_vaos.create(); }
Stage NewStage() { return g_stages.create(); }
Program NewProgram() { return g_programs.create(); }
Fbo NewPass() { return g_passes.create(); }

BufferObj* BufferAt(Buffer id) { return g_buffers.at(id); }
ImageObj* ImageAt(Texture id) { return g_images.at(id); }
SamplerObj* SamplerAt(Texture id) { return g_samplers.at(id); }
VaoObj* VaoAt(Vao id) { return g_vaos.at(id); }
StageObj* StageAt(Stage id) { return g_stages.at(id); }
ProgramObj* ProgramAt(Program id) { return g_programs.at(id); }
PassObj* PassAt(Fbo id) { return g_passes.at(id); }

// 走后端通的日志通道，保证 Vulkan 相关的报错真的落进 gvtcier.log，
// 而不是像 stderr 那样在 GUI 程序里被丢掉
void LogLine(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    rlog::Gvtcier_Log_Write("%s\n", buf);
}

// 引擎内部这套枚举是按 GL 的语义编的，这里只做查表，不改语义
Buffer CurrentBuffer(unsigned target) {
    switch (target) {
        case ArrayBuffer:         return g_state.curBuf[0];
        case ElementArrayBuffer:  return g_state.curBuf[1];
        case UniformBuffer:       return g_state.curBuf[2];
        case ShaderStorageBuffer: return g_state.curBuf[3];
        case PixelPackBuffer:     return g_state.curBuf[4];
        default:                  return 0;
    }
}

void SetCurrentBuffer(unsigned target, Buffer b) {
    switch (target) {
        case ArrayBuffer:         g_state.curBuf[0] = b; break;
        case ElementArrayBuffer:  g_state.curBuf[1] = b; break;
        case UniformBuffer:       g_state.curBuf[2] = b; break;
        case ShaderStorageBuffer: g_state.curBuf[3] = b; break;
        case PixelPackBuffer:     g_state.curBuf[4] = b; break;
        default: break;
    }
}

}
}
