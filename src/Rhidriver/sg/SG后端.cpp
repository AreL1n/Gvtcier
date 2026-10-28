#include "Rhidriver/backend.h"
#include "Rhidriver/sg/device_sg.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"
#include "Rhidriver/vk/vk.h"

#include <cstdio>

namespace rhi {
namespace sg {

// 驱动信息从 vk 上下文里取真实值，别再报假字符串
const char* t_Gvtcier_Rhi_GetString(unsigned name) {
    vk::VkContext& c = vk::ctx();
    switch (name) {
        case Version: {
            // 这里填的是物理设备的 apiVersion，跟 GL 后端报"4.6"的语义对得上
            static char buf[64];
            snprintf(buf, sizeof(buf), "%u.%u",
                     VK_API_VERSION_MAJOR(c.apiVersion), VK_API_VERSION_MINOR(c.apiVersion));
            return buf;
        }
        case RendererName:           return c.deviceName[0] ? c.deviceName : "Vulkan Device";
        case VendorName:             return "Vulkan";
        case ShadingLanguageVersion: return "SPIR-V";
        default:                     return "";
    }
}

// 版本号按 Vulkan 的 apiVersion 拆成主次版本，跟 GL 的字段语义对齐
void t_Gvtcier_Rhi_GetIntegerv(unsigned pname, int* data) {
    if (!data) return;
    vk::VkContext& c = vk::ctx();
    switch (pname) {
        case MajorVersion:   *data = (int)VK_API_VERSION_MAJOR(c.apiVersion); break;
        case MinorVersion:   *data = (int)VK_API_VERSION_MINOR(c.apiVersion); break;
        case MaxTextureSize: *data = 16384; break;
        case MaxSamples:     *data = 1; break;
        case MaxTexAniso:    *data = 16; break;
        default:             *data = 0; break;
    }
}

unsigned t_Gvtcier_Rhi_GetError() { return 0; }

// 渲染缓冲没有独立对象，sokol 里就是带 attachment 用途的图
void t_Gvtcier_Rhi_GenRenderbuffers(int n, Rbo* out) {
    for (int i = 0; i < n; ++i) out[i] = NewImage();
}

void t_Gvtcier_Rhi_BindRenderbuffer(unsigned target, Rbo r) { (void)target; (void)r; }

void t_Gvtcier_Rhi_DeleteRenderbuffers(int n, const Rbo* r) {
    for (int i = 0; i < n; ++i) {
        ImageObj* o = ImageAt(r[i]);
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

void t_Gvtcier_Rhi_RenderbufferStorage(unsigned target, unsigned internalformat, int w, int h) {
    (void)target; (void)internalformat; (void)w; (void)h;
}

void t_Gvtcier_Rhi_FramebufferRenderbuffer(unsigned target, unsigned attachment, unsigned rendertarget, Rbo r) {
    (void)rendertarget;
    t_Gvtcier_Rhi_FramebufferTexture2D(target, attachment, 0, (Texture)r, 0);
}

bool t_Gvtcier_Rhi_LegacyCreate() { return false; }
bool t_Gvtcier_Rhi_LegacyBind() { return false; }
bool t_Gvtcier_Rhi_CoreBind() { return false; }

static void sgPresent() { EndFrame(); }

static void sgSetVsync(bool vsync) { Dev().vsync = vsync; }

static void sgResize(int w, int h) {
    Dev().width = w;
    Dev().height = h;
    vk::ctx().pendingWidth = (uint32_t)w;
    vk::ctx().pendingHeight = (uint32_t)h;
    vk::ctx().pendingResize = true;
}

static void sgShutdown() { DestroyDevice(); }

// 表里的条目名就是 Gvtcier_Rhi_Xxx，sg 里对应实现是 t_Gvtcier_Rhi_Xxx
#define SG_RHI_ENTRY(ret, name, params, args) (ret (*) params) t_##name,
RhiTable g_sgTable = {
    sgPresent,
    sgSetVsync,
    sgResize,
    sgShutdown,

    RHI_TABLE_ENTRIES(SG_RHI_ENTRY)
};
#undef SG_RHI_ENTRY

}
}

namespace rhi {

RhiTable* sgTable(HWND hwnd) {
    if (!sg::CreateDevice((void*)hwnd, 1280, 720)) return nullptr;
    return &sg::g_sgTable;
}

}
