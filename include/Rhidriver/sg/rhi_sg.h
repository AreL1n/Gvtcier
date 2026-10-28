#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Rhidriver/rhi.h"
#include "Rhidriver/backend.h"
#include "sokol_gfx.h"

namespace rhi {
namespace sg {

// 引擎的句柄是 GL 风格的自增整数，sokol 用 32 位打包的 id。
// 这里两边记一份，销毁时把槽位置空，序号不复用。
struct BufferObj {
    sg_buffer buf{};
    unsigned target = 0;
    size_t size = 0;
    bool dynamic = false;
};

struct ImageObj {
    sg_image img{};
    sg_view view{};
    unsigned target = 0;
    int width = 0;
    int height = 0;
    int depth = 1;
    unsigned format = 0;
    bool isDepth = false;
};

struct SamplerObj {
    unsigned minFilter = LinearMipmapLinear;
    unsigned magFilter = Linear;
    unsigned wrapS = Repeat;
    unsigned wrapT = Repeat;
    unsigned wrapR = Repeat;
};

struct AttribDesc {
    unsigned index = 0;
    int size = 0;
    unsigned type = 0;
    bool normalized = false;
    int stride = 0;
    uint64_t offset = 0;
    unsigned divisor = 0;
};

struct VaoObj {
    std::vector<AttribDesc> attribs;
};

struct StageObj {
    unsigned type = 0;
    std::string source;
    std::vector<uint32_t> spirv;
    bool compiled = false;
    std::string log;
};

struct PassObj {
    std::array<Texture, 8> colors{};
    Texture depth = 0;
    int width = 0;
    int height = 0;
};

// 一个程序就是一条完整的绘制状态。sokol 要求管线在绘制前一次性建好，
// 所以这里一路记着属性、光栅、着色器，等第一次绘制时才真正创建。
struct ProgramObj {
    StageObj* stages[4] = {nullptr, nullptr, nullptr, nullptr};
    std::string tag;

    RasterState raster;

    sg_pipeline pipeline{};
    sg_shader shader{};
    std::string pipelineKey;

    std::vector<uint8_t> uniforms;
    std::unordered_map<std::string, int> uniformOffset;

    std::array<int, 16> samplerUnit{};
    std::array<Buffer, 16> ssbo{};

    bool compiled = true;
};

// 全后端的当前绑定状态，照着 GL 的语义维持一份当前值
struct BindState {
    std::array<Texture, 16> texUnit{};
    int activeUnit = 0;
    unsigned curBuf[8] = {};
    Vao curVao = 0;
    Program curProg = 0;
    Fbo curPass = 0;
    int vpX = 0, vpY = 0, vpW = 0, vpH = 0;
    unsigned primitive = Triangles;
    bool scissorOn = false;
    int scissorX = 0, scissorY = 0, scissorW = 0, scissorH = 0;
};

extern BindState g_state;

BufferObj* BufferAt(Buffer id);
ImageObj* ImageAt(Texture id);
SamplerObj* SamplerAt(Texture id);
VaoObj* VaoAt(Vao id);
StageObj* StageAt(Stage id);
ProgramObj* ProgramAt(Program id);
PassObj* PassAt(Fbo id);

Buffer NewBuffer();
Texture NewImage();
Vao NewVao();
Stage NewStage();
Program NewProgram();
Fbo NewPass();

void LogLine(const char* fmt, ...);

Buffer CurrentBuffer(unsigned target);
void SetCurrentBuffer(unsigned target, Buffer b);

// 把当前程序里攒下的状态落实成 sokol 管线
bool EnsurePipeline(ProgramObj& p);

}
}

// 后端函数表靠 RHI_TABLE_ENTRIES 展开，每个条目都会拼成 t_Gvtcier_Rhi_Xxx 的名字，
// 所以这里的实现必须登记一遍，不然拼出来的名字一个都找不到
namespace rhi {
namespace sg {

#define SG_DECL(ret, name, params, args) ret t_##name params;
RHI_TABLE_ENTRIES(SG_DECL)
#undef SG_DECL

}
}
