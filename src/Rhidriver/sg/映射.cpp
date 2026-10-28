#include "Rhidriver/sg/mapping_sg.h"

namespace rhi {
namespace sg {

// 引擎的纹理格式编号是 GL 风格，sokol 是一套自己的像素格式枚举，纯查表
sg_pixel_format ToPixelFormat(unsigned internalFormat) {
    switch (internalFormat) {
        case R8:                return SG_PIXELFORMAT_R8;
        case R16:               return SG_PIXELFORMAT_R16;
        case R16F:              return SG_PIXELFORMAT_R16F;
        case R32F:              return SG_PIXELFORMAT_R32F;
        case Rg8:               return SG_PIXELFORMAT_RG8;
        case Rg16F:             return SG_PIXELFORMAT_RG16F;
        case Rg32F:             return SG_PIXELFORMAT_RG32F;
        // sokol 没有三通道格式，统一用 RGBA 顶上，alpha 由上传数据补齐
        case Rgb8:              return SG_PIXELFORMAT_RGBA8;
        case Rgb16F:            return SG_PIXELFORMAT_RGBA16F;
        case Rgba8:             return SG_PIXELFORMAT_RGBA8;
        case Rgba16:            return SG_PIXELFORMAT_RGBA16;
        case Rgba16F:           return SG_PIXELFORMAT_RGBA16F;
        case Rgba32F:           return SG_PIXELFORMAT_RGBA32F;
        case Srgb8Alpha8:       return SG_PIXELFORMAT_SRGB8A8;
        case DepthComponent:    return SG_PIXELFORMAT_DEPTH;
        case DepthComponent16:  return SG_PIXELFORMAT_DEPTH;
        case DepthComponent24:  return SG_PIXELFORMAT_DEPTH_STENCIL;
        case DepthComponent32F: return SG_PIXELFORMAT_DEPTH;
        case Depth24Stencil8:   return SG_PIXELFORMAT_DEPTH_STENCIL;
        case R11G11B10F:        return SG_PIXELFORMAT_RG11B10F;
        default:                return SG_PIXELFORMAT_RGBA8;
    }
}

// Diligent 与 sokol 都没有三角扇，调用方必须自己换成三角形列表
sg_primitive_type ToPrimitive(unsigned mode) {
    switch (mode) {
        case Points:        return SG_PRIMITIVETYPE_POINTS;
        case Lines:         return SG_PRIMITIVETYPE_LINES;
        case LineStrip:     return SG_PRIMITIVETYPE_LINE_STRIP;
        case LineLoop:      return SG_PRIMITIVETYPE_LINE_STRIP;
        case Triangles:     return SG_PRIMITIVETYPE_TRIANGLES;
        case TriangleStrip: return SG_PRIMITIVETYPE_TRIANGLE_STRIP;
        case TriangleFan:   return SG_PRIMITIVETYPE_TRIANGLES;
        default:            return SG_PRIMITIVETYPE_TRIANGLES;
    }
}

sg_compare_func ToCompare(unsigned func) {
    switch (func) {
        case Never:    return SG_COMPAREFUNC_NEVER;
        case Less:     return SG_COMPAREFUNC_LESS;
        case EqualD:   return SG_COMPAREFUNC_EQUAL;
        case Lequal:   return SG_COMPAREFUNC_LESS_EQUAL;
        case Greater:  return SG_COMPAREFUNC_GREATER;
        case Notequal: return SG_COMPAREFUNC_NOT_EQUAL;
        case Gequal:   return SG_COMPAREFUNC_GREATER_EQUAL;
        case Always:   return SG_COMPAREFUNC_ALWAYS;
        default:       return SG_COMPAREFUNC_ALWAYS;
    }
}

sg_filter ToFilter(unsigned filter) {
    switch (filter) {
        case Nearest:
        case NearestMipmapNearest:
        case NearestMipmapLinear:
            return SG_FILTER_NEAREST;
        case Linear:
        case LinearMipmapNearest:
        case LinearMipmapLinear:
            return SG_FILTER_LINEAR;
        default:
            return SG_FILTER_LINEAR;
    }
}

sg_wrap ToWrap(unsigned wrap) {
    switch (wrap) {
        case Repeat:         return SG_WRAP_REPEAT;
        case MirroredRepeat: return SG_WRAP_MIRRORED_REPEAT;
        case ClampToEdge:    return SG_WRAP_CLAMP_TO_EDGE;
        case ClampToBorder:  return SG_WRAP_CLAMP_TO_EDGE;
        default:             return SG_WRAP_REPEAT;
    }
}

sg_blend_factor ToBlendFactor(unsigned factor) {
    switch (factor) {
        case Zero:             return SG_BLENDFACTOR_ZERO;
        case One:              return SG_BLENDFACTOR_ONE;
        case SrcAlpha:         return SG_BLENDFACTOR_SRC_ALPHA;
        case OneMinusSrcAlpha: return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case SrcColor:         return SG_BLENDFACTOR_SRC_COLOR;
        case OneMinusSrcColor: return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case DstAlpha:         return SG_BLENDFACTOR_DST_ALPHA;
        case OneMinusDstAlpha: return SG_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        default:               return SG_BLENDFACTOR_ONE;
    }
}

sg_blend_op ToBlendOp(bool add) {
    return add ? SG_BLENDOP_ADD : SG_BLENDOP_SUBTRACT;
}

sg_cull_mode ToCull(unsigned cullMode) {
    switch (cullMode) {
        case FrontAndBack: return SG_CULLMODE_NONE;
        case Front:        return SG_CULLMODE_FRONT;
        case Back:         return SG_CULLMODE_BACK;
        default:           return SG_CULLMODE_BACK;
    }
}

sg_face_winding ToWinding(unsigned frontFace) {
    return (frontFace == Ccw) ? SG_FACEWINDING_CCW : SG_FACEWINDING_CW;
}

sg_pixel_format ToSwapchainColor() { return SG_PIXELFORMAT_BGRA8; }
sg_pixel_format ToSwapchainDepth() { return SG_PIXELFORMAT_DEPTH; }

}
}
