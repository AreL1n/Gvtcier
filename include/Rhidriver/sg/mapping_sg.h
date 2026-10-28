#pragma once

#include "Rhidriver/rhi.h"
#include "sokol_gfx.h"

namespace rhi {
namespace sg {

sg_pixel_format ToPixelFormat(unsigned internalFormat);
sg_primitive_type ToPrimitive(unsigned mode);
sg_compare_func ToCompare(unsigned func);
sg_filter ToFilter(unsigned filter);
sg_wrap ToWrap(unsigned wrap);
sg_blend_factor ToBlendFactor(unsigned factor);
sg_blend_op ToBlendOp(bool add);
sg_cull_mode ToCull(unsigned cullMode);
sg_face_winding ToWinding(unsigned frontFace);
sg_pixel_format ToSwapchainColor();
sg_pixel_format ToSwapchainDepth();

}
}
