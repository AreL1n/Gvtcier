#include "Rhidriver/backend.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"

#include <cstring>

namespace rhi {
namespace sg {

static bool IsDepthFormat(unsigned f) {
    return f == DepthComponent || f == DepthComponent16 || f == DepthComponent24 ||
           f == DepthComponent32F || f == Depth24Stencil8;
}

// 每一层都按四通道算行宽，sokol 对 RGBA8 之类的格式就是这么要数据的
static int BytesPerPixel(unsigned internalFormat) {
    switch (internalFormat) {
        case R8: case R16: case R16F: case R32F: return 4;
        case Rg8: case Rg16F: case Rg32F: return 8;
        case Rgba16: case Rgba16F: case DepthComponent16: return 8;
        case Rgba32F: case Rgb16F: return 16;
        default: return 4;
    }
}

void t_Gvtcier_Rhi_GenTextures(int n, Texture* out) {
    for (int i = 0; i < n; ++i) out[i] = NewImage();
}

void t_Gvtcier_Rhi_DeleteTextures(int n, const Texture* t) {
    for (int i = 0; i < n; ++i) {
        ImageObj* o = ImageAt(t[i]);
        if (!o) continue;
        if (o->view.id != SG_INVALID_ID) {
            sg_destroy_view(o->view);
            o->view = sg_view{};
        }
        if (o->img.id != SG_INVALID_ID) {
            sg_destroy_image(o->img);
            o->img = sg_image{};
        }
    }
}

void t_Gvtcier_Rhi_BindTexture(unsigned target, Texture t) {
    if (g_state.activeUnit >= 0 && g_state.activeUnit < 16) g_state.texUnit[g_state.activeUnit] = t;
    ImageObj* o = ImageAt(t);
    if (o) o->target = target;
}

void t_Gvtcier_Rhi_BindTextureUnit(int unit, unsigned target, Texture t) {
    if (unit < 0 || unit >= 16) return;
    g_state.activeUnit = unit;
    g_state.texUnit[unit] = t;
    ImageObj* o = ImageAt(t);
    if (o) o->target = target;
}

// sokol 的图建好后只有 mip 0 可以整块传入，所以 level 非 0 的调用直接忽略，
// mip 链交给 GenerateMipmap 统一处理。
static void CreateImageImpl(unsigned target, int level, int internalFormat, int w, int h, int d,
                            unsigned format, unsigned type, const void* data) {
    if (level != 0) return;
    Texture id = g_state.texUnit[g_state.activeUnit];
    ImageObj* o = ImageAt(id);
    if (!o || !sg_isvalid()) return;

    if (o->view.id != SG_INVALID_ID) {
        sg_destroy_view(o->view);
        o->view = sg_view{};
    }
    if (o->img.id != SG_INVALID_ID) {
        sg_destroy_image(o->img);
        o->img = sg_image{};
    }

    bool depth = IsDepthFormat(internalFormat);
    sg_image_desc idesc{};
    idesc.width = w;
    idesc.height = h;
    idesc.pixel_format = ToPixelFormat(internalFormat);
    idesc.sample_count = 1;
    idesc.num_mipmaps = 1;

    if (target == Tex2DArray || target == Tex3D) {
        idesc.type = SG_IMAGETYPE_ARRAY;
        idesc.num_slices = d > 0 ? d : 1;
    } else if (target == TexCube || (target >= TexCubePosX && target <= TexCubeNegZ)) {
        idesc.type = SG_IMAGETYPE_CUBE;
        idesc.num_slices = 6;
    } else {
        idesc.type = SG_IMAGETYPE_2D;
        idesc.num_slices = 1;
    }

    idesc.usage.color_attachment = !depth;
    idesc.usage.depth_stencil_attachment = depth;

    if (data) {
        idesc.data.mip_levels[0].ptr = data;
        idesc.data.mip_levels[0].size = (size_t)w * (size_t)h * (size_t)idesc.num_slices *
                                        (size_t)BytesPerPixel(internalFormat);
    }

    o->img = sg_make_image(&idesc);
    if (o->img.id == SG_INVALID_ID) {
        LogLine("[sg] 纹理创建失败 %dx%d", w, h);
        return;
    }

    sg_view_desc vd{};
    if (depth) vd.depth_stencil_attachment.image = o->img;
    else vd.texture.image = o->img;
    o->view = sg_make_view(&vd);

    o->width = w;
    o->height = h;
    o->depth = idesc.num_slices;
    o->format = internalFormat;
    o->target = target;
    o->isDepth = depth;
}

void t_Gvtcier_Rhi_TexImage2D(unsigned target, int level, int internalFormat, int w, int h,
                              unsigned format, unsigned type, const void* data) {
    CreateImageImpl(target, level, internalFormat, w, h, 1, format, type, data);
}

void t_Gvtcier_Rhi_TexImage3D(unsigned target, int level, int internalFormat, int w, int h, int d,
                              unsigned format, unsigned type, const void* data) {
    CreateImageImpl(target, level, internalFormat, w, h, d, format, type, data);
}

void t_Gvtcier_Rhi_TexSubImage2D(unsigned target, int level, int x, int y, int w, int h,
                                 unsigned format, unsigned type, const void* data) {
    (void)target; (void)format; (void)type;
    ImageObj* o = ImageAt(g_state.texUnit[g_state.activeUnit]);
    if (!o || !data || o->img.id == SG_INVALID_ID) return;

    sg_write_image_desc wd{};
    wd.src.data.ptr = data;
    wd.src.data.size = (size_t)w * (size_t)h * (size_t)BytesPerPixel(o->format);
    wd.dst.image = o->img;
    wd.dst.mip_level = level;
    wd.dst.x = x;
    wd.dst.y = y;
    sg_write_image_transient(&wd);
}

void t_Gvtcier_Rhi_TexSubImage3D(unsigned target, int level, int x, int y, int z, int w, int h, int dd,
                                 unsigned format, unsigned type, const void* data) {
    (void)target; (void)format; (void)type;
    ImageObj* o = ImageAt(g_state.texUnit[g_state.activeUnit]);
    if (!o || !data || o->img.id == SG_INVALID_ID) return;

    sg_write_image_desc wd{};
    wd.src.data.ptr = data;
    wd.src.data.size = (size_t)w * (size_t)h * (size_t)dd * (size_t)BytesPerPixel(o->format);
    wd.dst.image = o->img;
    wd.dst.mip_level = level;
    wd.dst.x = x;
    wd.dst.y = y;
    wd.dst.slice = z;
    sg_write_image_transient(&wd);
}

void t_Gvtcier_Rhi_GenerateMipmap(unsigned target) { (void)target; }

void t_Gvtcier_Rhi_TexParameteri(unsigned target, unsigned pname, int value) {
    (void)target;
    SamplerObj* s = SamplerAt(g_state.texUnit[g_state.activeUnit]);
    if (!s) return;
    switch (pname) {
        case TexMinFilter: s->minFilter = (unsigned)value; break;
        case TexMagFilter: s->magFilter = (unsigned)value; break;
        case TexWrapS:     s->wrapS = (unsigned)value; break;
        case TexWrapT:     s->wrapT = (unsigned)value; break;
        case TexWrapR:     s->wrapR = (unsigned)value; break;
        default: break;
    }
}

void t_Gvtcier_Rhi_TexParameterf(unsigned target, unsigned pname, float value) {
    t_Gvtcier_Rhi_TexParameteri(target, pname, (int)value);
}

static int g_anisotropy = 1;
void t_Gvtcier_Rhi_SetAnisotropy(int level) { g_anisotropy = level < 1 ? 1 : (level > 16 ? 16 : level); }
int t_Gvtcier_Rhi_Anisotropy() { return g_anisotropy; }

}
}
