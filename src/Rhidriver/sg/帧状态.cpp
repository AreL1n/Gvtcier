#include "Rhidriver/backend.h"
#include "Rhidriver/sg/mapping_sg.h"
#include "Rhidriver/sg/rhi_sg.h"
#include "Rhidriver/vk/vk.h"

namespace rhi {
namespace sg {

static float g_clearColor[4] = {0, 0, 0, 1};
static float g_clearDepth = 1.0f;

void t_Gvtcier_Rhi_ClearColor(float r, float g, float b, float a) {
    g_clearColor[0] = r;
    g_clearColor[1] = g;
    g_clearColor[2] = b;
    g_clearColor[3] = a;
}

void t_Gvtcier_Rhi_ClearDepth(double d) { g_clearDepth = (float)d; }

// 清屏在 sokol 里是 pass action，属于 sg_begin_pass 的一部分，
// 所以这里只把值记下来，真正生效在下一帧开 pass 的时候。
void t_Gvtcier_Rhi_Clear(unsigned mask) {
    if (mask & ColorBit) vk::ctx().clearR = g_clearColor[0], vk::ctx().clearG = g_clearColor[1],
                         vk::ctx().clearB = g_clearColor[2], vk::ctx().clearA = g_clearColor[3];
    if (mask & DepthBit) vk::ctx().clearDepth = g_clearDepth;
}

void t_Gvtcier_Rhi_Viewport(int x, int y, int w, int h) {
    g_state.vpX = x;
    g_state.vpY = y;
    g_state.vpW = w;
    g_state.vpH = h;
    if (!sg_isvalid()) return;
    sg_apply_viewport(x, y, w, h, false);
    if (g_state.scissorOn)
        sg_apply_scissor_rect(g_state.scissorX, g_state.scissorY,
                              g_state.scissorW, g_state.scissorH, false);
}

void t_Gvtcier_Rhi_SetRasterState(const RasterState* s) {
    ProgramObj* p = ProgramAt(g_state.curProg);
    if (!p || !s) return;
    p->raster = *s;
    g_state.scissorOn = s->scissor;
    g_state.scissorX = s->scissorX;
    g_state.scissorY = s->scissorY;
    g_state.scissorW = s->scissorW;
    g_state.scissorH = s->scissorH;
}

void t_Gvtcier_Rhi_DrawBuffer(unsigned buf) { (void)buf; }
void t_Gvtcier_Rhi_DrawBuffers(int n, const unsigned* bufs) { (void)n; (void)bufs; }
void t_Gvtcier_Rhi_ReadBuffer(unsigned buf) { (void)buf; }

void t_Gvtcier_Rhi_ReadPixels(int x, int y, int w, int h, unsigned format, unsigned type, void* data) {
    (void)x; (void)y; (void)w; (void)h; (void)format; (void)type; (void)data;
}

void t_Gvtcier_Rhi_ReadBufferTex(unsigned format, unsigned type, unsigned tex) {
    (void)format; (void)type; (void)tex;
}

}
}
