#include "Rhidriver/backend.h"
#include "Rhidriver/sg/device_sg.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"

namespace rhi {
namespace sg {

static bool g_passOpen = false;
static sg_pass_action g_passAction{};

// 开 pass。sokol 要求每次绘制前显式开一帧的 pass，所以在这里把清屏值也一并带上。
static void OpenSwapchainPass() {
    if (g_passOpen) return;
    Device& d = Dev();
    if (!d.ready) return;

    g_passAction = sg_pass_action{};
    g_passAction.colors[0].load_action = SG_LOADACTION_CLEAR;
    g_passAction.colors[0].clear_value = sg_color{0.0f, 0.0f, 0.0f, 1.0f};
    g_passAction.depth.load_action = SG_LOADACTION_CLEAR;
    g_passAction.depth.clear_value = 1.0f;
    g_passAction.depth.store_action = SG_STOREACTION_STORE;
    g_passAction.colors[0].store_action = SG_STOREACTION_STORE;

    sg_pass pass{};
    pass.action = g_passAction;
    pass.swapchain = d.swapchain;
    sg_begin_pass(&pass);
    g_passOpen = true;
}

// 绘制前把所有绑定一次性提交给 sokol：管线、顶点缓冲、索引缓冲、纹理与采样器
static void ApplyBindings(ProgramObj& p) {
    sg_bindings b{};
    BufferObj* vbo = BufferAt(g_state.curBuf[0]);
    if (vbo && vbo->buf.id != SG_INVALID_ID) b.vertex_buffers[0] = vbo->buf;
    BufferObj* ibo = BufferAt(g_state.curBuf[1]);
    if (ibo && ibo->buf.id != SG_INVALID_ID) b.index_buffer = ibo->buf;

    for (int i = 0; i < 16; ++i) {
        ImageObj* img = ImageAt(g_state.texUnit[i]);
        if (img && img->view.id != SG_INVALID_ID) b.views[i] = img->view;
        SamplerObj* s = SamplerAt(g_state.texUnit[i]);
        if (s && img && img->view.id != SG_INVALID_ID) {
            sg_sampler_desc sd{};
            sd.min_filter = ToFilter(s->minFilter);
            sd.mag_filter = ToFilter(s->magFilter);
            sd.mipmap_filter = ToFilter(s->minFilter);
            sd.wrap_u = ToWrap(s->wrapS);
            sd.wrap_v = ToWrap(s->wrapT);
            sd.wrap_w = ToWrap(s->wrapR);
            b.samplers[i] = sg_make_sampler(&sd);
        }
    }
    sg_apply_bindings(&b);
}

static void DoDraw(ProgramObj& p, int base, int count, int instances, int baseVertex) {
    if (!sg_isvalid()) return;
    OpenSwapchainPass();
    if (!EnsurePipeline(p)) return;
    sg_apply_pipeline(p.pipeline);
    ApplyBindings(p);
    sg_draw_ex(base, count, instances, baseVertex, 0);
}

void t_Gvtcier_Rhi_DrawArrays(unsigned mode, int first, int count) {
    ProgramObj* p = ProgramAt(g_state.curProg);
    if (!p || count <= 0) return;
    g_state.primitive = mode;
    DoDraw(*p, first, count, 1, 0);
}

void t_Gvtcier_Rhi_DrawElements(unsigned mode, int count, unsigned type, const void* indices) {
    ProgramObj* p = ProgramAt(g_state.curProg);
    if (!p || count <= 0) return;
    g_state.primitive = mode;
    unsigned isize = (type == UnsignedShort) ? 2u : 4u;
    int base = (int)((uint32_t)(uintptr_t)indices / isize);
    DoDraw(*p, base, count, 1, 0);
}

void t_Gvtcier_Rhi_DrawElementsInstanced(unsigned mode, int count, unsigned type, const void* indices,
                                         int instanceCount) {
    ProgramObj* p = ProgramAt(g_state.curProg);
    if (!p || count <= 0) return;
    g_state.primitive = mode;
    unsigned isize = (type == UnsignedShort) ? 2u : 4u;
    int base = (int)((uint32_t)(uintptr_t)indices / isize);
    DoDraw(*p, base, count, instanceCount, 0);
}

// 帧缓冲在 sokol 里就是 pass 的附件集合，这里维护好颜色与深度附件的句柄
void t_Gvtcier_Rhi_GenFramebuffers(int n, Fbo* out) {
    for (int i = 0; i < n; ++i) out[i] = NewPass();
}

void t_Gvtcier_Rhi_DeleteFramebuffers(int n, const Fbo* f) {
    for (int i = 0; i < n; ++i) {
        PassObj* o = PassAt(f[i]);
        if (!o) continue;
        o->colors.fill(0);
        o->depth = 0;
    }
}

void t_Gvtcier_Rhi_BindFramebuffer(unsigned target, Fbo f) {
    (void)target;
    if (g_passOpen) {
        sg_end_pass();
        g_passOpen = false;
    }
    g_state.curPass = f;
}

void t_Gvtcier_Rhi_FramebufferTexture2D(unsigned target, unsigned attachment, unsigned textarget,
                                        Texture t, int level) {
    (void)target; (void)textarget; (void)level;
    PassObj* f = PassAt(g_state.curPass);
    if (!f) return;
    if (attachment == DepthAttachment || attachment == StencilAttachment || attachment == DepthStencilAttachment) {
        f->depth = t;
        return;
    }
    int idx = (int)attachment - (int)ColorAttachment0;
    if (idx < 0 || idx >= 8) return;
    f->colors[idx] = t;
    ImageObj* img = ImageAt(t);
    if (img) {
        f->width = img->width;
        f->height = img->height;
    }
}

void t_Gvtcier_Rhi_FramebufferTextureLayer(unsigned target, unsigned attachment, Texture t, int level, int layer) {
    (void)layer;
    t_Gvtcier_Rhi_FramebufferTexture2D(target, attachment, 0, t, level);
}

unsigned t_Gvtcier_Rhi_CheckFramebufferStatus(unsigned target) {
    (void)target;
    return FramebufferComplete;
}

}
}
