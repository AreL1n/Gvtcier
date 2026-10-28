#include "Rhidriver/sg/device_sg.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"

namespace rhi {
namespace sg {

// sokol 的顶点格式是按分量数和元素类型拼出来的枚举，这里查一张小表
static sg_vertex_format AttrFormat(int size, unsigned type, bool normalized) {
    bool f = (type == Float || type == HalfFloat);
    bool u = (type == UnsignedByte || type == UnsignedShort || type == UnsignedInt);
    bool s = (type == Byte || type == Short || type == Int);

    if (f) {
        switch (size) {
            case 1: return SG_VERTEXFORMAT_FLOAT;
            case 2: return SG_VERTEXFORMAT_FLOAT2;
            case 3: return SG_VERTEXFORMAT_FLOAT3;
            case 4: return SG_VERTEXFORMAT_FLOAT4;
            default: return SG_VERTEXFORMAT_INVALID;
        }
    }
    if (u) {
        if (normalized) {
            switch (size) {
                case 1: return SG_VERTEXFORMAT_UBYTE4N;
                case 2: return SG_VERTEXFORMAT_USHORT2N;
                case 4: return SG_VERTEXFORMAT_USHORT4N;
                default: return SG_VERTEXFORMAT_INVALID;
            }
        }
        switch (size) {
            case 1: return SG_VERTEXFORMAT_UBYTE4;
            case 2: return SG_VERTEXFORMAT_USHORT2;
            case 4: return SG_VERTEXFORMAT_USHORT4;
            default: return SG_VERTEXFORMAT_INVALID;
        }
    }
    if (s && normalized) {
        switch (size) {
            case 1: return SG_VERTEXFORMAT_BYTE4N;
            case 2: return SG_VERTEXFORMAT_SHORT2N;
            case 4: return SG_VERTEXFORMAT_SHORT4N;
            default: return SG_VERTEXFORMAT_INVALID;
        }
    }
    switch (size) {
        case 1: return SG_VERTEXFORMAT_INT;
        case 2: return SG_VERTEXFORMAT_INT2;
        case 3: return SG_VERTEXFORMAT_INT3;
        case 4: return SG_VERTEXFORMAT_INT4;
        default: return SG_VERTEXFORMAT_INVALID;
    }
}

// 管线的键要把帧缓冲、光栅状态、属性布局全算进去，任何一个变了都得重建
static std::string PipelineKey(const ProgramObj& p) {
    const RasterState& r = p.raster;
    std::string k;
    k.reserve(64);
    k += std::to_string((uintptr_t)p.stages[0]) + '|';
    k += std::to_string((uintptr_t)p.stages[1]) + '|';
    k += std::to_string(g_state.curPass) + '|';
    k += std::to_string(g_state.curVao) + '|';
    k += std::to_string(g_state.primitive) + '|';
    k += (r.depthTest ? "1" : "0");
    k += (r.depthWrite ? "1" : "0");
    k += std::to_string(r.depthFunc) + '|';
    k += (r.cull ? "1" : "0");
    k += std::to_string(r.cullMode) + '|';
    k += std::to_string(r.frontFace) + '|';
    k += (r.blend ? "1" : "0");
    k += std::to_string(r.blendSrc) + '|';
    k += std::to_string(r.blendDst) + '|';
    k += std::to_string(r.polyFactor) + '|';
    k += std::to_string(r.polyUnit) + '|';
    for (int i = 0; i < 4; ++i) k += r.colorWrite[i] ? "1" : "0";

    VaoObj* vao = VaoAt(g_state.curVao);
    if (vao) {
        for (const AttribDesc& a : vao->attribs) {
            k += std::to_string(a.index) + ':' + std::to_string(a.size) + ':' +
                 std::to_string(a.type) + ':' + std::to_string(a.stride) + ':' +
                 std::to_string(a.offset) + ':' + std::to_string(a.divisor) + ';';
        }
    }
    return k;
}

static bool BuildShader(ProgramObj& p) {
    if (!p.stages[0] || p.stages[0]->spirv.empty()) return false;

    sg_shader_desc sd{};
    if (p.stages[0] && !p.stages[0]->spirv.empty()) {
        sd.vertex_func.bytecode.ptr = p.stages[0]->spirv.data();
        sd.vertex_func.bytecode.size = p.stages[0]->spirv.size() * sizeof(uint32_t);
        sd.vertex_func.entry = "main";
    }
    if (p.stages[1] && !p.stages[1]->spirv.empty()) {
        sd.fragment_func.bytecode.ptr = p.stages[1]->spirv.data();
        sd.fragment_func.bytecode.size = p.stages[1]->spirv.size() * sizeof(uint32_t);
        sd.fragment_func.entry = "main";
    }

    sg_shader sh = sg_make_shader(&sd);
    if (sh.id == SG_INVALID_ID) {
        LogLine("[sg] 着色器创建失败 %s", p.tag.c_str());
        return false;
    }
    p.shader = sh;
    return true;
}

bool EnsurePipeline(ProgramObj& p) {
    if (!sg_isvalid()) return false;
    if (!p.stages[0] || p.stages[0]->spirv.empty()) return false;

    std::string key = PipelineKey(p);
    if (p.pipeline.id != SG_INVALID_ID && p.pipelineKey == key) return true;

    if (p.pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(p.pipeline);
        p.pipeline = sg_pipeline{};
    }
    if (p.shader.id == SG_INVALID_ID && !BuildShader(p)) return false;

    const RasterState& r = p.raster;
    sg_pipeline_desc pd{};
    pd.shader = p.shader;
    pd.primitive_type = ToPrimitive(g_state.primitive);
    pd.index_type = SG_INDEXTYPE_NONE;
    pd.cull_mode = r.cull ? ToCull(r.cullMode) : SG_CULLMODE_NONE;
    pd.face_winding = ToWinding(r.frontFace);
    pd.sample_count = 1;

    pd.depth.pixel_format = ToSwapchainDepth();
    pd.depth.compare = r.depthTest ? ToCompare(r.depthFunc) : SG_COMPAREFUNC_ALWAYS;
    pd.depth.write_enabled = r.depthWrite;
    pd.depth.bias = r.polyUnit;
    pd.depth.bias_slope_scale = r.polyFactor;

    pd.color_count = 1;
    pd.colors[0].pixel_format = ToSwapchainColor();
    pd.colors[0].blend.enabled = r.blend;
    pd.colors[0].blend.src_factor_rgb = ToBlendFactor(r.blendSrc);
    pd.colors[0].blend.dst_factor_rgb = ToBlendFactor(r.blendDst);
    pd.colors[0].blend.op_rgb = ToBlendOp(true);
    pd.colors[0].blend.src_factor_alpha = ToBlendFactor(r.blendSrc);
    pd.colors[0].blend.dst_factor_alpha = ToBlendFactor(r.blendDst);
    pd.colors[0].blend.op_alpha = ToBlendOp(true);
    sg_color_mask mask = SG_COLORMASK_NONE;
    if (r.colorWrite[0]) mask = (sg_color_mask)(mask | SG_COLORMASK_R);
    if (r.colorWrite[1]) mask = (sg_color_mask)(mask | SG_COLORMASK_G);
    if (r.colorWrite[2]) mask = (sg_color_mask)(mask | SG_COLORMASK_B);
    if (r.colorWrite[3]) mask = (sg_color_mask)(mask | SG_COLORMASK_A);
    pd.colors[0].write_mask = mask;

    VaoObj* vao = VaoAt(g_state.curVao);
    if (vao && !vao->attribs.empty()) {
        pd.layout.buffers[0].stride = vao->attribs[0].stride;
        pd.layout.buffers[0].step_func = vao->attribs[0].divisor > 0 ? SG_VERTEXSTEP_PER_INSTANCE
                                                                     : SG_VERTEXSTEP_PER_VERTEX;
        pd.layout.buffers[0].step_rate = vao->attribs[0].divisor > 0 ? (int)vao->attribs[0].divisor : 1;
        for (const AttribDesc& a : vao->attribs) {
            if (a.index >= SG_MAX_VERTEX_ATTRIBUTES) continue;
            pd.layout.attrs[a.index].buffer_index = 0;
            pd.layout.attrs[a.index].offset = (int)a.offset;
            pd.layout.attrs[a.index].format = AttrFormat(a.size, a.type, a.normalized);
        }
    }

    p.pipeline = sg_make_pipeline(&pd);
    if (p.pipeline.id == SG_INVALID_ID) {
        LogLine("[sg] 管线创建失败 %s", p.tag.c_str());
        return false;
    }
    p.pipelineKey = key;
    return true;
}

}
}
