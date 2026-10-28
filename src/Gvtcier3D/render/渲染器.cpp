#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "Gvtcier.h"
#ifdef GVWCIER_USE_GLES
#else
#include <windows.h>
#endif
#include <iterator>
#include <cwchar>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <chrono>
#include <functional>

using namespace rhi;

namespace {

// 阴影滤波与偏移的引擎默认值。调试窗只临时覆盖，引擎不依赖那份数据
struct CsmDefaults {
    float lightBias;
    float normalBias;
    float slopeClamp;
    float diskRadius;
    float pcfRadius;
    float zPad;
    float biasScale;
    float blendBand;
};

constexpr CsmDefaults kCsmDefaults{ 0.0f, 10.0f, 0.0f, 10.0f, 0.87f, 0.0f, 1.79f, 0.1f };

// 阴影质量档位到阴影贴图边长的映射。
//   1 -> 1024（低，老显卡/省显存）
//   2 -> gv::kShadowMapSize，也就是 2048（引擎默认档）
//   3 -> 3072（高，边缘更细腻，代价是显存与采样带宽翻倍多）
//   其他值 -> 原样返回 current，表示“不改变当前设置”
int shadowSizeForQuality(int quality, int current) {
    switch (quality) {
        case 1: return 1024;
        case 2: return gv::kShadowMapSize;
        case 3: return 3072;
        default: return current;
    }
}

// 描述一次绘制所需光栅状态的“临时结构”，各 pass 在栈上构造一份、改几个字段再交给
// applyPassState。默认值就是主几何 pass 的常规状态：
//   depthTest 开、depthWrite 开、depthFunc = Greater（反向 Z：越近的深度值越大，所以“大于等于才通过”）、
//   剔背面、正面按逆时针判定、不混合、不裁剪、填充模式
// 之所以每个 pass 都从默认值重建而不是“在上一状态上改”，是为了避免 pass 之间状态互相污染。
struct PassState
{
    bool depthTest = true;
    bool depthWrite = true;
    unsigned depthFunc = Greater;
    bool cull = true;
    unsigned cullMode = Back;
    unsigned frontFace = Ccw;
    bool blend = false;
    unsigned blendSrc = SrcAlpha;
    unsigned blendDst = OneMinusSrcAlpha;
    bool scissor = false;
    unsigned polygonMode = Fill;
    float polyFactor = 0.0f;
    float polyUnit = 0.0f;
};

// 当前已生效的光栅状态缓存。RHI 层按状态做去重，所以这里维护一份可以少发无效调用，
// 也方便某些 pass 临时改一两个字段（scissor、blend）后直接用 rsFlush 提交。
static rhi::RasterState g_rs;

// 把 PassState 展开成 RHI 的光栅状态并立即提交，同时更新缓存 g_rs。
// 所有 pass 都通过它来切换状态。
void applyPassState(const PassState& st)
{
    rhi::RasterState rs;
    rs.depthTest = st.depthTest;
    rs.depthWrite = st.depthWrite;
    rs.depthFunc = st.depthFunc;
    rs.cull = st.cull;
    rs.cullMode = st.cullMode;
    rs.frontFace = st.frontFace;
    rs.blend = st.blend;
    rs.blendSrc = st.blendSrc;
    rs.blendDst = st.blendDst;
    rs.scissor = st.scissor;
    rs.polygonMode = st.polygonMode;
    rs.polyFactor = st.polyFactor;
    rs.polyUnit = st.polyUnit;
    g_rs = rs;
    Gvtcier_Rhi_SetRasterState(&g_rs);
}

// 把当前缓存的状态原样再提交一次。用于代码直接改了 g_rs 的若干字段之后立即生效。
void rsFlush() { Gvtcier_Rhi_SetRasterState(&g_rs); }

}

// 引擎运行时间（秒），从第一次调用这个函数的时刻开始计时，单调递增。
// 给天空动画、着色器里的 uTime 用；和帧率无关，所以暂停/卡顿也不会跳变。
// 注意：t0 是函数内静态变量，首次调用发生在第一次渲染时，不是进程启动时。
static double engineTime() {
    static auto t0 = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - t0).count();
}

#ifdef GVWCIER_USE_GLES
// GLES 后端下，像素缓冲对象（PBO）映射要先确认扩展确实被加载了。
// 回读 HZB 深度图和 1x1 亮度探针都靠 PBO 做异步回读，缺了这两个入口就只能退化成同步回读。
static bool gvPboMapSupported() {
    return gles.glMapBufferRange != nullptr && gles.glUnmapBuffer != nullptr;
}
// 映射当前绑定的 PBO 以便 CPU 直接读。0x88EB=PIXEL_PACK_BUFFER，
// 访问标志 0x0001|0x0002 = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT。
static void* gvMapPixelBuffer(GLintptr len) {
    if (!gvPboMapSupported()) return nullptr;
    return gles.glMapBufferRange(0x88EB, 0, len, 0x0001 | 0x0002);
}
static void gvUnmapPixelBuffer() { if (gvPboMapSupported()) gles.glUnmapBuffer(0x88EB); }
static void gvReadPixels(GLint x, GLint y, GLint w, GLint h, GLenum f, GLenum t, void* d) {
    gles.glReadPixels(x, y, w, h, f, t, d);
}
#else
// 桌面 OpenGL 后端：只有走 GL 时才支持 PBO 映射，其他后端（如软件/空后端）一律返回 false。
static bool gvPboMapSupported() { return rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Gl; }
// 0x88EB=PIXEL_PACK_BUFFER，0x88B8=GL_READ_ONLY，只读映射已写入的 PBO。
static void* gvMapPixelBuffer(GLintptr) { return gl.glMapBuffer(0x88EB, 0x88B8); }
static void gvUnmapPixelBuffer() { gl.glUnmapBuffer(0x88EB); }
// 桌面端只有 GL 后端有真正的 glReadPixels；其他后端静默跳过，不做回读。
static void gvReadPixels(GLint x, GLint y, GLint w, GLint h, GLenum f, GLenum t, void* d) {
    if (rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Gl) gl.glReadPixels(x, y, w, h, f, t, d);
}
// 把源帧缓冲的深度附件 blit 到目标帧缓冲，支持缩放。
// 用途是把全分辨率深度一次性缩到 128x128 给 HZB 遮挡剔除用，比逐像素读回快得多。
// sw/sh 是源尺寸，dw/dh 是目标尺寸；0x8CA8=GL_READ_FRAMEBUFFER，0x8CA9=GL_DRAW_FRAMEBUFFER，
// 0x00000100=GL_DEPTH_BUFFER_BIT，最后一位 0 表示最近点采样（不插值深度）。
// 返回 false 表示当前后端不支持 blit，调用方需要放弃这次 HZB 更新。
static bool gvBlitDepth(unsigned srcFbo, int sw, int sh, unsigned dstFbo, int dw, int dh) {
    if (rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Gl && gl.glBlitFramebuffer) {
        gl.glBindFramebuffer(0x8CA8, srcFbo);
        gl.glBindFramebuffer(0x8CA9, dstFbo);
        gl.glBlitFramebuffer(0, 0, sw, sh, 0, 0, dw, dh, 0x00000100, 0);
        gl.glBindFramebuffer(0x8CA8, 0);
        gl.glBindFramebuffer(0x8CA9, 0);
        return true;
    }
#ifdef GVWCIER_USE_GLES
    if (gles.glBlitFramebuffer) {
        gles.glBindFramebuffer(0x8CA8, srcFbo);
        gles.glBindFramebuffer(0x8CA9, dstFbo);
        gles.glBlitFramebuffer(0, 0, sw, sh, 0, 0, dw, dh, 0x00000100, 0);
        gles.glBindFramebuffer(0x8CA8, 0);
        gles.glBindFramebuffer(0x8CA9, 0);
        return true;
    }
#endif
    (void)srcFbo; (void)sw; (void)sh; (void)dstFbo; (void)dw; (void)dh;
    return false;
}
#endif

// 特效粒子用的顶点格式。每颗粒子只提交一份数据，四个角点由顶点着色器
// 按 gl_VertexID 展开，CPU 不再重复算四遍世界坐标。
//   pos      粒子世界中心
//   r,g,b,a  颜色与透明度（会按寿命衰减到 a 上）
//   size     世界尺寸，展开时乘到角点上
//   rotation 绕视线自转角度
//   ageKind  age 是归一化年龄 0~1，kind 是粒子类型编号
//   facing   朝向模式，0 面向相机 / 1 只绕 Y / 2 顺速度 / 3 顶点已展开（光束）
struct FxVertex {
    float pos[3];
    float r, g, b, a;
    float size;
    float rotation;
    float ageKind[2];
    float facing;
    float vel[3];
};

// 取对象的世界变换矩阵，内部由对象自己缓存，脏了才重算。
static Mat4 modelMatrix(const Object& o) {
    return o.Gvtcier_Object_Matrix();
}

// 按 ID 从场景里取对象，找不到返回空指针。
// 可见性列表里存的是 BVH 索引，要经过 objIndexBuf_ 换成 ID 再取对象，
// 中间可能对象已被销毁，所以每次都要判空。
static const Object* resolveObj(const Scene& scene, GvtId<Object> id) {
    return scene.Gvtcier_Scene_GetObject(id);
}

// 暴露粒子系统给外部（脚本/游戏逻辑）加粒子、光束、动态光。
FxSystem& Renderer::Gvtcier_Render_Fx() { return fx_; }

void Renderer::Gvtcier_Render_SetFlashlight(bool on) { flashlightOn_ = on; }

// 渲染器一次性初始化：加载全部着色器、建天空球、建各类 VAO/VBO/SSBO、建阴影图。
// 顺序上的几个要点：
//   - 先重置着色器失败计数，最后再统一检查，这样一次就能报出所有编译失败的着色器
//   - 所有着色器都从内嵌资源里按编号取（res_get_shader(编号)），编号与 res.rc 一致
//   - 主着色器和实例化着色器必须有；后处理类（GTAO、体积光）缺资源时只是跳过，不算致命错误
//   - 天空球半径 900：取这么大是为了让天空整个罩住场景（相机远平面 2000），
//     同时避免球面顶点在深度精度差的远端产生裂缝；绘制时不做深度测试所以它总在最后面
//   - 反向 Z 约定在同一处统一设置：ClearDepth(0) + depthFunc Greater
// 返回 false 表示关键资源缺失或着色器编译失败，此时渲染器不可用。
bool Renderer::Gvtcier_Render_Init() {
    Gvtcier_Shadow_Tuning_Init();
    effectSystem_.Gvtcier_Effect_SetAssetStore(&effectAssets_);
    shader_guard::Gvtcier_Shader_ResetFailures();
    int mvLen = 0, mfLen = 0;
    const char* mv = (const char*)res_get_shader(0, &mvLen);
    const char* mf = (const char*)res_get_shader(1, &mfLen);
    if (!mv || !mf) { printf("[renderer] missing main shader source\n"); return false; }
    if (!mainShader_.Gvtcier_Shader_LoadFromMemory(mv, mvLen, mf, mfLen)) return false;
    {
        int mivLen = 0, mifLen = 0;
        const char* miv = (const char*)res_get_shader(2, &mivLen);
        const char* mif = (const char*)res_get_shader(3, &mifLen);
        if (!miv || !mif) { printf("[renderer] missing main_inst shader source\n"); return false; }
        if (!mainInstShader_.Gvtcier_Shader_LoadFromMemory(miv, mivLen, mif, mifLen)) return false;
    }
    {
        int dlfLen = 0;
        const char* dlf = (const char*)res_get_shader(4, &dlfLen);
        if (!dlf) { printf("[renderer] missing deferred light shader source\n"); return false; }
        int pvLen = 0;
        const char* pv = (const char*)res_get_shader(26, &pvLen);
        if (!pv) { printf("[renderer] missing post vert source\n"); return false; }
        if (!deferredLightShader_.Gvtcier_Shader_LoadFromMemory(pv, pvLen, dlf, dlfLen)) return false;
    }
    int svLen = 0, sfLen = 0;
    const char* sv = (const char*)res_get_shader(11, &svLen);
    const char* sf = (const char*)res_get_shader(12, &sfLen);
    if (!sv || !sf) { printf("[renderer] missing skinned shader source\n"); return false; }
    if (!skinnedShader_.Gvtcier_Shader_LoadFromMemory(sv, svLen, sf, sfLen)) return false;
    int dvLen = 0, dfLen = 0;
    const char* dv = (const char*)res_get_shader(5, &dvLen);
    const char* df = (const char*)res_get_shader(6, &dfLen);
    if (!dv || !df) return false;
    if (!depthShader_.Gvtcier_Shader_LoadFromMemory(dv, dvLen, df, dfLen)) return false;
    {
        int pvLen = 0, pfLen = 0;
        const char* pvs = (const char*)res_get_shader(35, &pvLen);
        const char* pfs = (const char*)res_get_shader(36, &pfLen);
        if (pvs && pfs) pointDepthShader_.Gvtcier_Shader_LoadFromMemory(pvs, pvLen, pfs, pfLen);
    }
    int dsvLen = 0;
    const char* dsv = (const char*)res_get_shader(7, &dsvLen);
    if (!dsv) return false;
    if (!depthSkinnedShader_.Gvtcier_Shader_LoadFromMemory(dsv, dsvLen, df, dfLen)) return false;
    int gzvLen = 0, gzfLen = 0;
    const char* gzv = (const char*)res_get_shader(10, &gzvLen);
    const char* gzf = (const char*)res_get_shader(28, &gzfLen);
    if (!gzv || !gzf) return false;
    if (!gizmoShader_.Gvtcier_Shader_LoadFromMemory(gzv, gzvLen, gzf, gzfLen)) return false;
    int tvLen = 0, tfLen = 0;
    const char* tv = (const char*)res_get_shader(29, &tvLen);
    const char* tf = (const char*)res_get_shader(31, &tfLen);
    if (!tv || !tf) return false;
    if (!textShader_.Gvtcier_Shader_LoadFromMemory(tv, tvLen, tf, tfLen)) return false;
    int ivLen = 0, ifLen = 0;
    const char* iv = (const char*)res_get_shader(29, &ivLen);
    const char* iff = (const char*)res_get_shader(30, &ifLen);
    if (!iv || !iff) return false;
    if (!imageShader_.Gvtcier_Shader_LoadFromMemory(iv, ivLen, iff, ifLen)) return false;
    if (!font_.init(96, 2048)) return false;
    // 文本/图片共用的 VAO：一个顶点 4 个 float = 屏幕坐标 xy + uv，纹理坐标在 [0,1]
    rhi::Gvtcier_Rhi_GenVertexArrays(1, &textVao_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &textVbo_);
    rhi::Gvtcier_Rhi_BindVertexArray(textVao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, textVbo_);
    rhi::Gvtcier_Rhi_VertexAttribPointer(0, 2, Float, false, 4 * sizeof(float), (const void*)0);
    rhi::Gvtcier_Rhi_EnableAttrib(0);
    rhi::Gvtcier_Rhi_VertexAttribPointer(1, 2, Float, false, 4 * sizeof(float), (const void*)(2 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(1);
    rhi::Gvtcier_Rhi_BindVertexArray(0);
    int rvLen = 0, rfLen = 0;
    const char* rv = (const char*)res_get_shader(27, &rvLen);
    const char* rf = (const char*)res_get_shader(28, &rfLen);
    if (!rv || !rf) return false;
    if (!rect2dShader_.Gvtcier_Shader_LoadFromMemory(rv, rvLen, rf, rfLen)) return false;
    // 2D 矩形/圆角面板共用 VAO：每个顶点 6 个 float = 屏幕坐标 xy + rgba 颜色
    rhi::Gvtcier_Rhi_GenVertexArrays(1, &rect2dVao_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &rect2dVbo_);
    rhi::Gvtcier_Rhi_BindVertexArray(rect2dVao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, rect2dVbo_);
    rhi::Gvtcier_Rhi_VertexAttribPointer(0, 2, Float, false, 6 * sizeof(float), (const void*)0);
    rhi::Gvtcier_Rhi_EnableAttrib(0);
    rhi::Gvtcier_Rhi_VertexAttribPointer(1, 4, Float, false, 6 * sizeof(float), (const void*)(2 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(1);
    rhi::Gvtcier_Rhi_BindVertexArray(0);
    int xvLen = 0, xfLen = 0;
    const char* xv = (const char*)res_get_shader(8, &xvLen);
    const char* xf = (const char*)res_get_shader(9, &xfLen);
    if (!xv || !xf) return false;
    if (!fxShader_.Gvtcier_Shader_LoadFromMemory(xv, xvLen, xf, xfLen)) return false;
    rhi::Gvtcier_Rhi_GenVertexArrays(1, &fxVao_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &fxVbo_);
    rhi::Gvtcier_Rhi_BindVertexArray(fxVao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, fxVbo_);
    rhi::Gvtcier_Rhi_VertexAttribPointer(0, 3, Float, false, sizeof(FxVertex), (const void*)0);
    rhi::Gvtcier_Rhi_EnableAttrib(0);
    rhi::Gvtcier_Rhi_VertexAttribPointer(1, 4, Float, false, sizeof(FxVertex), (const void*)(3 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(1);
    rhi::Gvtcier_Rhi_VertexAttribPointer(2, 1, Float, false, sizeof(FxVertex), (const void*)(7 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(2);
    rhi::Gvtcier_Rhi_VertexAttribPointer(3, 1, Float, false, sizeof(FxVertex), (const void*)(8 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(3);
    rhi::Gvtcier_Rhi_VertexAttribPointer(4, 2, Float, false, sizeof(FxVertex), (const void*)(9 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(4);
    rhi::Gvtcier_Rhi_VertexAttribPointer(5, 1, Float, false, sizeof(FxVertex), (const void*)(11 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(5);
    rhi::Gvtcier_Rhi_VertexAttribPointer(6, 3, Float, false, sizeof(FxVertex), (const void*)(12 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(6);
    rhi::Gvtcier_Rhi_BindVertexArray(0);

    // 调试线框用的 VAO：每个顶点 3 个 float 位置 + 4 个 float 颜色
    rhi::Gvtcier_Rhi_GenVertexArrays(1, &dbgLineVao_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &dbgLineVbo_);
    rhi::Gvtcier_Rhi_BindVertexArray(dbgLineVao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, dbgLineVbo_);
    rhi::Gvtcier_Rhi_VertexAttribPointer(0, 3, Float, false, sizeof(DebugLineVertex), (const void*)0);
    rhi::Gvtcier_Rhi_EnableAttrib(0);
    rhi::Gvtcier_Rhi_VertexAttribPointer(1, 4, Float, false, sizeof(DebugLineVertex), (const void*)(3 * sizeof(float)));
    rhi::Gvtcier_Rhi_EnableAttrib(1);
    rhi::Gvtcier_Rhi_BindVertexArray(0);

    int pvLen2 = 0, bfLen2 = 0, lfLen2 = 0, cfLen2 = 0;
    const char* pv2 = (const char*)res_get_shader(26, &pvLen2);
    const char* bf2 = (const char*)res_get_shader(15, &bfLen2);
    const char* lf2 = (const char*)res_get_shader(16, &lfLen2);
    const char* cf2 = (const char*)res_get_shader(17, &cfLen2);
    if (!pv2 || !bf2 || !lf2 || !cf2) return false;
    if (!brightShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, bf2, bfLen2)) return false;
    if (!blurShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, lf2, lfLen2)) return false;
    if (!composeShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, cf2, cfLen2)) return false;
    int glLen = 0;
    const char* glf = (const char*)res_get_shader(32, &glLen);
    if (!glf) return false;
    if (!glassShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, glf, glLen)) return false;
    int skyvLen = 0, skyfLen = 0;
    const char* skyv = (const char*)res_get_shader(13, &skyvLen);
    const char* skyf = (const char*)res_get_shader(14, &skyfLen);
    if (!skyv || !skyf) return false;
    if (!skyShader_.Gvtcier_Shader_LoadFromMemory(skyv, skyvLen, skyf, skyfLen)) return false;
    int lvLen = 0;
    const char* lf = (const char*)res_get_shader(20, &lvLen);
    if (!lf) return false;
    if (!lumShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, lf, lvLen)) return false;
    int faLen = 0;
    const char* faf = (const char*)res_get_shader(33, &faLen);
    if (!faf) return false;
    if (!fxaaShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, faf, faLen)) return false;
    {
        int gLen = 0, grLen = 0, gbLen = 0;
        const char* gf = (const char*)res_get_shader(18, &gLen);
        const char* gbf = (const char*)res_get_shader(19, &gbLen);
        if (gf && gbf) {
            if (!gtaoShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, gf, gLen)) return false;
            if (!gtaoBlurShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, gbf, gbLen)) return false;
        }
    }
    {
        int vfLen = 0, vbLen = 0;
        const char* vf = (const char*)res_get_shader(21, &vfLen);
        const char* vbf = (const char*)res_get_shader(22, &vbLen);
        if (vf && vbf) {
            if (!volShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, vf, vfLen)) return false;
            if (!volBlurShader_.Gvtcier_Shader_LoadFromMemory(pv2, pvLen2, vbf, vbLen)) return false;
        }
    }
    if (!volShadow_.create(gv::kShadowMapSize)) return false;
    // 太阳阴影是 4 层级的纹理数组，层数由 gv::kCascadeCount 决定（默认 4）
    sunShadow_.createLayered(gv::kShadowMapSize, gv::kCascadeCount);
    asmShadow_.createLayered(256, kAsmSlots);
    for (int i = 0; i < kPointShadowLights; ++i) pointShadow_[i].create(512);
    // 体积光需要一张 3D 噪声纹理，开销不大所以在初始化时就烘焙好，之后每帧复用
    bakeVolNoise();
    {
        int ovLen = 0, ofLen = 0;
        const char* ov = (const char*)res_get_shader(23, &ovLen);
        const char* of = (const char*)res_get_shader(24, &ofLen);
        if (!ov || !of) return false;
        if (!outlineShader_.Gvtcier_Shader_LoadFromMemory(ov, ovLen, of, ofLen)) return false;
        int osvLen = 0;
        const char* osv = (const char*)res_get_shader(25, &osvLen);
        if (!osv) return false;
        if (!outlineSkinnedShader_.Gvtcier_Shader_LoadFromMemory(osv, osvLen, of, ofLen)) return false;
    }

    // 全屏三角覆盖用的四边形，坐标直接是裁剪空间的 [-1,1]，
    // 顶点着色器原样输出即可，所有后处理 pass 都拿它当画布
    Vertex fv[4] = {
        { Vec3(-1, -1, 0), Vec3(0, 0, 1), Vec2(0, 0) },
        { Vec3( 1, -1, 0), Vec3(0, 0, 1), Vec2(1, 0) },
        { Vec3( 1,  1, 0), Vec3(0, 0, 1), Vec2(1, 1) },
        { Vec3(-1,  1, 0), Vec3(0, 0, 1), Vec2(0, 1) },
    };
    unsigned int fi[6] = { 0, 1, 2, 0, 2, 3 };
    if (!fullscreenQuad_.create(fv, 4, fi, 6)) return false;
    // 天空球半径 1999：比最大视距 2000 小 1，罩住整个视锥
    skySphere_ = makeSphere(1999.0f, 48, 64);
    // 天空渐变查找纹理：1D（实际用宽 256 高 1 的 2D 纹理存），内容随天空颜色变化重建
    if (!skyGradLut_) rhi::Gvtcier_Rhi_GenTextures(1, &skyGradLut_);

    // 建立反向 Z 的初始约定：清深度为 0（最远），之后所有 pass 统一用 depthFunc=Greater
    rhi::Gvtcier_Rhi_ClearDepth(0.0);
    {
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    // 三张 SSBO：光源数据、tile 表（个数 + 偏移）、tile 内的光源索引
    rhi::Gvtcier_Rhi_GenBuffers(1, &lightSsbo_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &tileSsbo_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &idxSsbo_);

    int shaderFails = shader_guard::Gvtcier_Shader_FailureCount();
    if (shaderFails > 0) {
        printf("[renderer] %d shader failure(s), first: %s\n",
               shaderFails, shader_guard::Gvtcier_Shader_FirstFailure());
        return false;
    }

    printf("[renderer] initialized\n");
    return true;
}

// 释放渲染器持有的全部 GPU 资源，顺序与初始化相反。
// 会一并销毁所有帧缓冲、阴影图、天空球、字体纹理、特效资源与全部着色器程序；
// 调用后渲染器回到未初始化状态，可以再次 Init。
void Renderer::Gvtcier_Render_Shutdown() {
    if (fxVao_) rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &fxVao_);
    if (fxVbo_) rhi::Gvtcier_Rhi_DeleteBuffers(1, &fxVbo_);
    if (textVao_) rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &textVao_);
    if (textVbo_) rhi::Gvtcier_Rhi_DeleteBuffers(1, &textVbo_);
    if (rect2dVao_) rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &rect2dVao_);
    if (rect2dVbo_) rhi::Gvtcier_Rhi_DeleteBuffers(1, &rect2dVbo_);
    if (lightSsbo_) rhi::Gvtcier_Rhi_DeleteBuffers(1, &lightSsbo_);
    if (tileSsbo_) rhi::Gvtcier_Rhi_DeleteBuffers(1, &tileSsbo_);
    if (idxSsbo_) rhi::Gvtcier_Rhi_DeleteBuffers(1, &idxSsbo_);
    font_.destroy();
    fullscreenQuad_.destroy();
    skySphere_.destroy();
    if (skyGradLut_) { rhi::Gvtcier_Rhi_DeleteTextures(1, &skyGradLut_); skyGradLut_ = 0; }
    skyLutColor_ = Vec3(-1.0f, -1.0f, -1.0f);
    volShadow_.destroy();
    if (volNoiseTex_) { rhi::Gvtcier_Rhi_DeleteTextures(1, &volNoiseTex_); volNoiseTex_ = 0; }
    for (int i = 0; i < 2; ++i) {
        if (volFbo_[i]) { rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &volFbo_[i]); volFbo_[i] = 0; }
        if (volTex_[i]) { rhi::Gvtcier_Rhi_DeleteTextures(1, &volTex_[i]); volTex_[i] = 0; }
    }
    hdrFbo_.destroy();
    ldrFbo_.destroy();
    lumFbo_.destroy();
    gtaoFbo_[0].destroy();
    gtaoFbo_[1].destroy();
    gtaoFbo_[2].destroy();
    textEntityTexs_.clear();
    for (int i = 0; i < 4; ++i) bloomMips_[i].destroy();
    for (int i = 0; i < 4; ++i) glassFbo_[i].destroy();
    if (glassSceneTex_) { rhi::Gvtcier_Rhi_DeleteTextures(1, &glassSceneTex_); glassSceneTex_ = 0; }
    effectAssets_.Gvtcier_Object_Clear();
    effectAssetIds_.clear();
    releaseSceneFont();

    // 统一销毁所有着色器程序，与 Init 里的加载顺序对应
    mainShader_.Gvtcier_Shader_Destroy();
    depthShader_.Gvtcier_Shader_Destroy();
    fxShader_.Gvtcier_Shader_Destroy();
    gizmoShader_.Gvtcier_Shader_Destroy();
    skinnedShader_.Gvtcier_Shader_Destroy();
    depthSkinnedShader_.Gvtcier_Shader_Destroy();
    brightShader_.Gvtcier_Shader_Destroy();
    blurShader_.Gvtcier_Shader_Destroy();
    glassShader_.Gvtcier_Shader_Destroy();
    composeShader_.Gvtcier_Shader_Destroy();
    skyShader_.Gvtcier_Shader_Destroy();
    pointDepthShader_.Gvtcier_Shader_Destroy();
    lumShader_.Gvtcier_Shader_Destroy();
    fxaaShader_.Gvtcier_Shader_Destroy();
    gtaoShader_.Gvtcier_Shader_Destroy();
    gtaoBlurShader_.Gvtcier_Shader_Destroy();
    volShader_.Gvtcier_Shader_Destroy();
    volBlurShader_.Gvtcier_Shader_Destroy();
    outlineShader_.Gvtcier_Shader_Destroy();
    outlineSkinnedShader_.Gvtcier_Shader_Destroy();
    textShader_.Gvtcier_Shader_Destroy();
    rect2dShader_.Gvtcier_Shader_Destroy();
}

// 卡通描边（反向外壳法）开关，on 非 0 开启。
void Renderer::Gvtcier_Render_SetToon(int on) { toonOn_ = (on != 0); }
// 查询卡通描边是否开启，返回 0/1。
int Renderer::Gvtcier_Render_GetToon() const { return toonOn_ ? 1 : 0; }
// FXAA 抗锯齿开关。开着时 compose 先写 LDR 缓冲再跑一遍 FXAA 到屏幕。
void Renderer::Gvtcier_Render_SetFxaa(int on) { fxaaOn_ = (on != 0); }
// 查询 FXAA 开关状态。
int Renderer::Gvtcier_Render_GetFxaa() const { return fxaaOn_ ? 1 : 0; }
// 隐藏游戏自己的 UI（不影响 BeginEngineUI 之后的引擎 UI），
// 用于截图、过场动画等场景。
void Renderer::Gvtcier_Render_SetGameUIHidden(bool hidden) { suppressGameUI_ = hidden; }
// 设置卡通描边宽度，单位是裁剪空间下的屏幕空间偏移量，越大描边越粗。
void Renderer::Gvtcier_Render_SetOutlineWidth(float w) { outlineW_ = w; }
// 读当前描边宽度。
float Renderer::Gvtcier_Render_GetOutlineWidth() const { return outlineW_; }

// 开关 HZB 遮挡剔除。关掉后会退化成只做视锥剔除，画面更保守但也更慢。
void Renderer::Gvtcier_Render_SetOcclusion(bool on) { occlusionOn_ = on; }
// 查询遮挡剔除开关。
int Renderer::Gvtcier_Render_GetOcclusion() const { return occlusionOn_ ? 1 : 0; }
// 上一帧被遮挡剔除掉的对象数量，用于性能面板显示。
int Renderer::Gvtcier_Render_OccludedCount() const { return occludedCount_; }

// 构建 CPU 侧 HZB（层次深度缓冲）的替代物：一张 128x128 的降采样深度图。
// 因为这里没有做完整的 mip 链，而是直接拿这一个最小层做保守的“深度立方体”测试，
// 所以实现上是“有就用来剔除，没有就跳过”的软性优化。
//
// 关键做法：
//   - 每两帧才更新一次（hzbTick & 1），复用时沿用上一帧的视图/投影矩阵，
//     同时把视口恢复成主视口，避免影响后续 pass；深度图滞后一帧的代价换来的是省一次昂贵回读
//   - gvBlitDepth 把全分辨率深度缩到 128x128，比逐像素 glReadPixels 快一个量级
//   - 支持 PBO 时用双缓冲异步回读：这次发起读取、下次再映射取结果，避免 GPU 同步等待
//   - 反向 Z 下深度值越大越近，所以后面 hzbTest 里的比较是“物体最近深度是否仍然更远”
// 前置条件：occlusionOn_ 打开、HDR 帧缓冲带深度附件。
void Renderer::buildHzb(Scene& scene, const Camera& cam, const RenderOptions& opt) {
    (void)scene;
    occludedCount_ = 0;
    if (!occlusionOn_) { hzbValid_ = false; return; }
    if (!hdrFbo_.depthTex()) return;
    const int kHzb = 128;
    static int hzbTick = 0;
    // 隔帧更新：奇数次调用直接复用上一帧的深度图与矩阵，只把视口改回主视口
    if ((hzbTick++ & 1) != 0 && hzbValid_) {
        int rw3 = 0, rh3 = 0;
        renderRes(rw3, rh3, opt);
        rhi::Gvtcier_Rhi_Viewport(0, 0, rw3, rh3);
        hzbView_ = cam.viewMatrix();
        hzbProj_ = cam.projMatrix((float)opt.width / (float)opt.height);
        return;
    }
    hzbValid_ = false;
    if (hzbCopyFbo_.width() != kHzb || hzbCopyFbo_.height() != kHzb)
        hzbCopyFbo_.create(kHzb, kHzb, true);
    int rw0 = 0, rh0 = 0;
    renderRes(rw0, rh0, opt);
    // 把全分辨率深度一次性缩到 128x128；不支持 blit 的后端直接放弃本帧 HZB
    if (!gvBlitDepth(hdrFbo_.fbo(), rw0, rh0, hzbCopyFbo_.fbo(), kHzb, kHzb)) return;

    int w = kHzb, h = kHzb;
    size_t readBytes = (size_t)w * (size_t)h * 4;
    const bool usePbo = gvPboMapSupported();
    if (usePbo) {
        // 先把上一帧发起的异步读取回来：双缓冲轮流用，读另一张才是已经填好的
        if (hzbPboPending_) {
            int done = 1 - hzbPboIndex_;
            rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, hzbPbo_[done]);
            const void* mapped = gvMapPixelBuffer((GLintptr)readBytes);
            if (mapped) {
                hzbReadBuf_.resize(readBytes);
                memcpy(hzbReadBuf_.data(), mapped, readBytes);
                gvUnmapPixelBuffer();
                hzbDepth_.resize((size_t)w * (size_t)h);
                const float* depth32 = (const float*)hzbReadBuf_.data();
                for (size_t i = 0; i < (size_t)w * (size_t)h; ++i)
                    hzbDepth_[i] = depth32[i];
                hzbW_ = w;
                hzbH_ = h;
                hzbValid_ = true;
            }
            hzbPboPending_ = false;
        }
        rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, hzbCopyFbo_.fbo());
        if (hzbPbo_[0] == 0) rhi::Gvtcier_Rhi_GenBuffers(2, hzbPbo_);
        // 换一张 PBO 发起本帧的回读，0x1902=DEPTH_COMPONENT，0x1406=FLOAT（32 位深度图）
        hzbPboIndex_ = 1 - hzbPboIndex_;
        rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, hzbPbo_[hzbPboIndex_]);
        rhi::Gvtcier_Rhi_BufferData(rhi::PixelPackBuffer, readBytes, nullptr, rhi::StreamRead);
        gvReadPixels(0, 0, w, h, 0x1902, 0x1406, nullptr);
        hzbPboPending_ = true;
        rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, 0);
        rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, 0);
    } else {
        // 没有 PBO 时的同步回读路径，会阻塞等 GPU 画完，所以只在必要时用
        rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, hzbCopyFbo_.fbo());
        hzbReadBuf_.resize(readBytes);
        gvReadPixels(0, 0, w, h, 0x1902, 0x1406, hzbReadBuf_.data());
        rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, 0);
        hzbDepth_.resize((size_t)w * (size_t)h);
        const float* depth32 = (const float*)hzbReadBuf_.data();
        for (size_t i = 0; i < (size_t)w * (size_t)h; ++i)
            hzbDepth_[i] = depth32[i];
        hzbW_ = w;
        hzbH_ = h;
        hzbValid_ = true;
    }
    hzbView_ = cam.viewMatrix();
    hzbProj_ = cam.projMatrix((float)opt.width / (float)opt.height);
    int rw2 = 0, rh2 = 0;
    renderRes(rw2, rh2, opt);
    rhi::Gvtcier_Rhi_Viewport(0, 0, rw2, rh2);
}

// 用 HZB 深度图判断一个包围盒是否被完全遮挡。
// 返回 true 表示“确定被遮挡”，可以从可见列表里剔除；返回 false 表示“没被遮挡或无法判断”，
// 一律按可见处理——宁可多画也不能漏画。
//
// 算法：
//   1. 把包围盒 8 个角点变换到裁剪空间，求出屏幕空间的 AABB（bx0..by1）和最大 NDC z（pz）
//      pz 是所有角点里最靠前的深度，也就是物体最靠近相机的那一面
//   2. 只要有角点跑到相机后面（w<=0），或者包围盒部分超出屏幕，直接放弃剔除，保证安全
//   3. 把屏幕 AABB 换算成 128x128 深度图的像素范围，再向外扩 2 像素，
//      抵消降采样带来的误差；范围过大（超过半屏）也不剔，因为粗略包围意义不大
//   4. 把最近深度转成 [0,1] 的窗口深度，反向 Z 下也就是 nearZ 越大越近；
//      再加上 0.008 的深度偏置，避免物体贴合背景时被自己“遮挡”
//   5. 逐个像素比较：只要发现某个像素的背景深度比物体最近面还小（更远），
//      说明这个位置没有被遮挡，立刻返回 false；全部像素都比物体更近才算完全遮挡
// 参数 b 是世界空间包围盒，view/proj 是对应 HZB 生成时的矩阵（隔帧更新时要与深度图配套）。
bool Renderer::hzbTest(const AABB3& b, const Mat4& view, const Mat4& proj) const {
    if (!hzbValid_) return false;
    Vec3 mn = aabbMin(b);
    Vec3 mx = aabbMax(b);
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f, pz = -1e30f;
    bool anyFront = false;
    for (int c = 0; c < 8; ++c) {
        Vec3 p((c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z);
        Vec4 clip = proj * view * Vec4(p.x, p.y, p.z, 1.0f);
        if (clip.w <= 1e-5f) continue;
        anyFront = true;
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        float ndcZ = clip.z / clip.w;
        if (ndcX < bx0) bx0 = ndcX;
        if (ndcY < by0) by0 = ndcY;
        if (ndcX > bx1) bx1 = ndcX;
        if (ndcY > by1) by1 = ndcY;
        if (ndcZ > pz) pz = ndcZ;
    }
    if (!anyFront) return false;
    // 先判完全在屏幕外（早退），再判是否部分出屏（部分出屏就不做遮挡剔除）
    if (bx1 < -1.0f || by1 < -1.0f || bx0 > 1.0f || by0 > 1.0f) return false;
    if (bx0 < -1.0f || by0 < -1.0f || bx1 > 1.0f || by1 > 1.0f) return false;
    float u0 = bx0 * 0.5f + 0.5f;
    float v0 = by0 * 0.5f + 0.5f;
    float u1 = bx1 * 0.5f + 0.5f;
    float v1 = by1 * 0.5f + 0.5f;
    int x0 = (int)(u0 * (float)hzbW_) - 2;
    int y0 = (int)(v0 * (float)hzbH_) - 2;
    int x1 = (int)(u1 * (float)hzbW_ + 0.999f) + 2;
    int y1 = (int)(v1 * (float)hzbH_ + 0.999f) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > hzbW_) x1 = hzbW_;
    if (y1 > hzbH_) y1 = hzbH_;
    if (x1 <= x0 || y1 <= y0) return false;
    if (x1 - x0 > hzbW_ / 2 || y1 - y0 > hzbH_ / 2) return false;
    float nearZ = pz * 0.5f + 0.5f;
    // 0.008 的深度偏置，抵消 128x128 降采样误差与光栅化抖动，避免误剔
    const float kDepthBias = 0.008f;
    nearZ += kDepthBias;
    for (int y = y0; y < y1; ++y) {
        const float* row = hzbDepth_.data() + (size_t)y * (size_t)hzbW_;
        for (int x = x0; x < x1; ++x) {
            // 背景这个像素比物体最近面还远，说明物体在这里露出来了
            if (nearZ >= row[x]) return false;
        }
    }
    return true;
}
// 设置卡通描边颜色，分量范围 0~1。
void Renderer::Gvtcier_Render_SetOutlineColor(float r, float g, float b) {
    outlineCol_[0] = r; outlineCol_[1] = g; outlineCol_[2] = b;
}
// 读取当前描边颜色到调用方给的三个输出指针，指针允许为空（表示不关心该分量）。
void Renderer::Gvtcier_Render_GetOutlineColor(float* r, float* g, float* b) const {
    if (r) *r = outlineCol_[0];
    if (g) *g = outlineCol_[1];
    if (b) *b = outlineCol_[2];
}

// 应用画质设置。
// 参数：
//   shadowQuality     阴影质量档 1/2/3，映射到 1024/2048/3072，其他值表示保持不变
//   anisotropic       各向异性过滤档 0~3，对应 1/2/4/8 倍
//   specular          是否开启高光
//   autoExposure      是否开启自动曝光
//   lightQuality      光照质量，会改每个 tile 的光源上限
//   fogQuality        雾等级 0~3，0 关闭、1 距离雾、2/3 体积光（等级越高越浓）
//   vignetteStrength  暗角强度
// 副作用：阴影尺寸变化时会重建体积阴影与级联阴影贴图，失败则回退到原尺寸。
// 重建前先查硬件最大纹理尺寸，避免请求超过上限导致创建失败。
void Renderer::Gvtcier_Render_ApplySettings(int shadowQuality, int anisotropic, bool specular,
                                            bool autoExposure, int lightQuality, int fogQuality,
                                            float vignetteStrength) {
    specularOn_ = specular;
    autoExposureOn_ = autoExposure;
    vignette_ = vignetteStrength;
    Gvtcier_Limits_ApplyQuality(lightQuality);
    lightLimit_ = Gvtcier_Limits().maxLightsPerTile;
    fogLevel_ = (fogQuality < 0) ? 0 : (fogQuality > 3 ? 3 : fogQuality);

    int want = shadowSizeForQuality(shadowQuality, shadowSize_);
    int maxTex = 0;
    rhi::Gvtcier_Rhi_GetIntegerv(rhi::MaxTextureSize, &maxTex);
    if (maxTex > 0 && want > maxTex) want = maxTex;
    if (want != shadowSize_) {
        volShadow_.destroy();
        sunShadow_.destroy();
        if (volShadow_.create(want) && sunShadow_.createLayered(want, gv::kCascadeCount)) {
            shadowSize_ = want;
        } else {
            volShadow_.create(shadowSize_);
            sunShadow_.createLayered(shadowSize_, gv::kCascadeCount);
        }
    }

    static const int kAnisoVals[4] = { 1, 2, 4, 8 };
    // 档位越界时夹到 0~3，映射成 1/2/4/8 倍各向异性，交给 RHI 记录供纹理创建时使用
    int ai = (anisotropic < 0) ? 0 : (anisotropic > 3 ? 3 : anisotropic);
    rhi::Gvtcier_Rhi_SetAnisotropy(kAnisoVals[ai]);
}

// 把一行文字加进 2D 绘制队列，真正的绘制发生在帧末的 FlushCanvas。
// 参数：x/y 是左上角像素坐标，scale 是字号缩放倍数，utf8 是 UTF-8 文本，
// z 是层序（小的先画），r/g/b/a 是颜色，a 是整体透明度。
void Renderer::Gvtcier_Render_DrawText(int x, int y, float scale, const char* utf8, int z, float r, float g, float b, float a) {
    CanvasPrim p;
    p.type = CanvasPrim::Text;
    p.x = (float)x; p.y = (float)y;
    p.z = z;
    p.scale = scale;
    p.text = utf8;
    p.r = r; p.g = g; p.b = b; p.a = a;
    pushCanvasPrim(p);
}

// 进入引擎 UI 模式：之后压入的 2D 图元会被归到 engineItems_，
// 即使游戏 UI 被隐藏（suppressGameUI_）也照样绘制，保证调试面板始终可见。
void Renderer::Gvtcier_Render_BeginEngineUI() {
    writeEngine_ = true;
}

// 退出引擎 UI 模式，恢复成普通游戏 UI 队列。
void Renderer::Gvtcier_Render_EndEngineUI() {
    writeEngine_ = false;
}

// 2D 图元的统一入队口：引擎 UI 模式进 engineItems_，否则进 canvasItems_；
// 游戏 UI 被隐藏时游戏图元直接丢弃。
void Renderer::pushCanvasPrim(const CanvasPrim& p) {
    if (writeEngine_) engineItems_.push_back(p);
    else if (!suppressGameUI_) canvasItems_.push_back(p);
}

// 画一个 2D 实心矩形，参数依次是左上角 x/y、宽高 w/h、颜色 rgba 与层序 z。
// 只是入队，实际绘制在 FlushCanvas 里按 z 排序后批量提交。
void Renderer::Gvtcier_Render_DrawRect2D(float x, float y, float w, float h, float r, float g, float b, float a, int z) {
    CanvasPrim p;
    p.type = CanvasPrim::Rect;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.r = r; p.g = g; p.b = b; p.a = a;
    p.z = z;
    pushCanvasPrim(p);
}

// 画一张原始 GL 纹理到 2D 画布上，位置尺寸由 x/y/w/h 给出，a 是透明度。
// tex 必须是调用方已经持有的 GL 纹理名，渲染器不接管它的生命周期。
void Renderer::Gvtcier_Render_DrawImage(unsigned int tex, float x, float y, float w, float h, float a, int z) {
    CanvasPrim p;
    p.type = CanvasPrim::Image;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.r = 1.0f; p.g = 1.0f; p.b = 1.0f; p.a = a;
    p.z = z;
    p.tex = tex;
    pushCanvasPrim(p);
}

// 请求一块毛玻璃面板。这里只记录参数放进 glassPanels_，真正的模糊与合成
// 在帧末的 glassPass 里做（那时的屏幕内容已经画完，才拿得到正确的背景）。
// radius 是圆角半径，alpha 是面板整体不透明度，tint 是叠加的灰度染色强度。
void Renderer::Gvtcier_Render_DrawGlass(float x, float y, float w, float h, float radius, float alpha, float tint, int z) {
    if (w <= 0.0f || h <= 0.0f) return;
    GlassPanel g;
    g.x = x; g.y = y; g.w = w; g.h = h;
    g.radius = radius; g.alpha = alpha; g.tint = tint;
    g.z = z;
    glassPanels_.push_back(g);
}

// 量一段文字的尺寸。outW 得到总前进宽度，outH 得到实际像素高度，
// outTop 得到文字顶部相对基线原点的偏移（用于垂直居中）。
// 高度只统计有实际像素的字形，所以空字符串或全是空格的文本高度会是 0。
void Renderer::Gvtcier_Render_MeasureTextEx(const char* utf8, float scale, float* outW, float* outH, float* outTop) {
    // 字形按 kBasePx 栅格，scale=1 想要的是原来的 32px 视觉尺寸，所以统一按这个比例换算
    const float px = scale * (32.0f / (float)FontAtlas::kBasePx);
    float w = 0.0f;
    float top = 1e30f;
    float bottom = -1e30f;
    if (utf8) {
        const char* s = utf8;
        while (*s) {
            unsigned int cp = gvUtf8Next(s);
            const FontGlyph* g = font_.glyph(cp);
            if (!g) continue;
            w += g->advance * px;
            if (g->w > 0 && g->h > 0) {
                float y0 = (font_.ascent() - g->oy) * px;
                float y1 = y0 + g->h * px;
                if (y0 < top) top = y0;
                if (y1 > bottom) bottom = y1;
            }
        }
    }
    bool any = (bottom > top);
    if (outW) *outW = w;
    if (outH) *outH = any ? (bottom - top) : 0.0f;
    if (outTop) *outTop = any ? top : 0.0f;
}

// 只要宽高的简化版测字，内部转调 MeasureTextEx。
void Renderer::Gvtcier_Render_MeasureText(const char* utf8, float scale, float* outW, float* outH) {
    Gvtcier_Render_MeasureTextEx(utf8, scale, outW, outH, nullptr);
}

// 切换纯 2D 模式。开启后 Gvtcier_Render_Render 只画 2D 画布，
// 不再做任何 3D pass（用于编辑器/纯菜单界面，省掉整条 3D 管线开销）。
void Renderer::Gvtcier_Render_Set2DMode(bool on) {
    mode2D_ = on;
}

// 烘焙体积光用的 3D 噪声纹理（32×32×32 单通道）。
// 做法是 4 个八度的值噪声叠加：
//   hashf 用整数位运算把格点坐标散列成 0~1 的伪随机数，不依赖随机表，结果可复现
//   vnoise 在格点之间做三线性插值，插值权重再用 3t²-2t³ 平滑，避免格子感
//   每个八度频率乘 2.03（大于 2 一点，避免各层网格对齐产生规律条纹），振幅减半（0.5 起始）
// 叠加完夹到 [0,1] 后量化成字节。
// 纹理用 Repeat 寻址，所以体渲染时噪声是无限平铺的，不会看到边界接缝。
// 生成后整个生命周期只创建这一次（初始化时调），之后每帧采样复用。
void Renderer::bakeVolNoise() {
    const int kNoiseSize = 32;
    std::vector<unsigned char> data((size_t)kNoiseSize * kNoiseSize * kNoiseSize);
    auto hashf = [](int x, int y, int z) {
        unsigned h = (unsigned)(x * 374761393 + y * 668265263 + z * 2147483647u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)((h ^ (h >> 16)) & 0xFFFFFFu) / (float)0xFFFFFFu;
    };
    auto vnoise = [&](float x, float y, float z) {
        int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
        float xf = x - (float)xi, yf = y - (float)yi, zf = z - (float)zi;
        xf = xf * xf * (3.0f - 2.0f * xf);
        yf = yf * yf * (3.0f - 2.0f * yf);
        zf = zf * zf * (3.0f - 2.0f * zf);
        float c000 = hashf(xi, yi, zi), c100 = hashf(xi + 1, yi, zi);
        float c010 = hashf(xi, yi + 1, zi), c110 = hashf(xi + 1, yi + 1, zi);
        float c001 = hashf(xi, yi, zi + 1), c101 = hashf(xi + 1, yi, zi + 1);
        float c011 = hashf(xi, yi + 1, zi + 1), c111 = hashf(xi + 1, yi + 1, zi + 1);
        float x00 = c000 + (c100 - c000) * xf;
        float x10 = c010 + (c110 - c010) * xf;
        float x01 = c001 + (c101 - c001) * xf;
        float x11 = c011 + (c111 - c011) * xf;
        float y0 = x00 + (x10 - x00) * yf;
        float y1 = x01 + (x11 - x01) * yf;
        return y0 + (y1 - y0) * zf;
    };
    for (int z = 0; z < kNoiseSize; ++z)
        for (int y = 0; y < kNoiseSize; ++y)
            for (int x = 0; x < kNoiseSize; ++x) {
                float v = 0.0f, amp = 0.5f;
                float fx = (float)x * 0.25f, fy = (float)y * 0.25f, fz = (float)z * 0.25f;
                for (int o = 0; o < 4; ++o) {
                    v += amp * vnoise(fx, fy, fz);
                    fx *= 2.03f; fy *= 2.03f; fz *= 2.03f;
                    amp *= 0.5f;
                }
                data[((size_t)z * kNoiseSize + y) * kNoiseSize + x] =
                    (unsigned char)(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f);
            }
    rhi::Gvtcier_Rhi_GenTextures(1, &volNoiseTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex3D, volNoiseTex_);
    rhi::Gvtcier_Rhi_TexImage3D(Tex3D, 0, R8, kNoiseSize, kNoiseSize, kNoiseSize, Red, UnsignedByte, data.data());
    rhi::Gvtcier_Rhi_TexParameteri(Tex3D, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex3D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex3D, TexWrapS, Repeat);
    rhi::Gvtcier_Rhi_TexParameteri(Tex3D, TexWrapT, Repeat);
    rhi::Gvtcier_Rhi_TexParameteri(Tex3D, TexWrapR, Repeat);
}

// 渲染体积光/体积雾用的一张正交深度图：从太阳方向看整个场景。
// 体积光在 ray march 时要在每个采样点判断“这里有没有被太阳照到”，
// 靠这张阴影图判断；它只关心“能否到达太阳”，所以不需要级联，一张正交图包住全场景即可。
//
// 结果缓存策略：为了让体积光和太阳阴影“一动就重算、不动就跳过”，这里算了一个场景签名 sig
// （FNV 风格的哈希，把每个投影物体的位置/缩放/旋转/ID 混进去），
// 再比较光照方向与上一帧是否一致。三者都没变且上次结果有效，就直接复用，只恢复视口。
// 注意：只要场景里有蒙皮网格，sig 就强制翻转为不等于上一帧，逼着每帧重算，
// 因为蒙皮动画的顶点变化无法从对象属性里体现出来。
//
// 输出：volLightVp_（太阳的正交视图投影矩阵）与 volLightValid_，后续体积光 pass 依赖它们。
void Renderer::volumetricShadowPass(Scene& scene) {
    if (volShadow_.size() <= 0) { volLightValid_ = false; return; }
    // scene.light.dir 记录的是光线传播方向，太阳在它的反方向
    Vec3 L(-scene.light.dir.x, -scene.light.dir.y, -scene.light.dir.z);
    float ll = L.length();
    if (ll > 1e-6f) L = L / ll;
    else L = Vec3(0, 1, 0);

    unsigned sig = 0;
    // 计算场景签名：把每个投影物体的位置、统一缩放、ID、欧拉角按位塞进 32 位哈希。
    // 16777619 是经典的 FNV-1a 质数。
    scene.ForEachObject([&](const Object& o, GvtId<Object> id) {
        const Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
        if (!o.visible || !o.castShadow || !om) return;
        if (sig == 0) sig = (unsigned)scene.Gvtcier_Scene_ObjectCount();
        float bits[8];
        bits[0] = o.pos.x; bits[1] = o.pos.y; bits[2] = o.pos.z;
        bits[3] = o.scale.x * o.scale.y * o.scale.z;
        bits[4] = (float)id.index;
        bits[5] = o.rotDeg.x; bits[6] = o.rotDeg.y; bits[7] = o.rotDeg.z;
        for (int k = 0; k < 8; ++k) {
            unsigned u = 0;
            const char* bp = (const char*)&bits[k];
            u |= (unsigned)(unsigned char)bp[0];
            u |= (unsigned)(unsigned char)bp[1] << 8;
            u |= (unsigned)(unsigned char)bp[2] << 16;
            u |= (unsigned)(unsigned char)bp[3] << 24;
            sig = sig * 16777619u ^ u;
        }
    });
    // 有蒙皮网格时签名恒不同，等价于每帧强制重算（蒙皮顶点动画无法用对象属性表示）
    if (scene.hasSkinned()) sig = lastVolSig_ ^ 1u;
    bool lightMoved = std::fabs(L.x - lastVolLightDir_.x) > 1e-4f ||
                      std::fabs(L.y - lastVolLightDir_.y) > 1e-4f ||
                      std::fabs(L.z - lastVolLightDir_.z) > 1e-4f;
    // 场景没变、光没动、上次结果有效：直接跳过整个重算，只把渲染目标恢复成 HDR 帧缓冲
    if (!volShadowDirty_ && !lightMoved && sig == lastVolSig_ && volLightValid_) {
        hdrFbo_.bind();
        rhi::Gvtcier_Rhi_Viewport(0, 0, viewW_, viewH_);
        return;
    }
    volShadowDirty_ = false;
    lastVolSig_ = sig;
    lastVolLightDir_ = L;

    Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    bool any = false;
    scene.ForEachObject([&](const Object& o, GvtId<Object>) {
        const Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
        if (!o.visible || !o.castShadow || !om) return;
        Vec3 h = scene.Gvtcier_Scene_MeshHalfExtents(o.mesh);
        h = Vec3(h.x < 0.0f ? -h.x : h.x, h.y < 0.0f ? -h.y : h.y, h.z < 0.0f ? -h.z : h.z);
        h = Vec3(h.x * o.scale.x, h.y * o.scale.y, h.z * o.scale.z);
        Vec3 a = o.pos - h;
        Vec3 b = o.pos + h;
        mn.x = std::fmin(mn.x, a.x); mn.y = std::fmin(mn.y, a.y); mn.z = std::fmin(mn.z, a.z);
        mx.x = std::fmax(mx.x, b.x); mx.y = std::fmax(mx.y, b.y); mx.z = std::fmax(mx.z, b.z);
        any = true;
    });
    if (!any) { volLightValid_ = false; return; }

    Vec3 center = (mn + mx) * 0.5f;
    // 正交包围球半径：1.15 的余量保证旋转后也不裁角，+2 再留一点绝对边距
    float radius = (mx - center).length() * 1.15f + 2.0f;
    Vec3 up(0, 1, 0);
    // 太阳接近垂直照射时，世界 Y 轴不能当 up（会和视线平行导致矩阵退化），换 Z 轴
    if (std::fabs(L.dot(up)) > 0.95f) up = Vec3(0, 0, 1);
    // 相机放在包围球外 back 距离处，near 取 0.1、far 覆盖到球后方，保证整个场景都在深度范围内
    float back = radius + 10.0f;
    Mat4 view = Mat4::lookAt(center + L * back, center, up);
    Mat4 proj = Mat4::ortho(-radius, radius, -radius, radius, 0.1f, back + radius + 10.0f);
    volLightVp_ = proj * view;
    volLightValid_ = true;

    volShadow_.bind();
    rhi::Gvtcier_Rhi_Viewport(0, 0, volShadow_.size(), volShadow_.size());
    rhi::Gvtcier_Rhi_ClearDepth(0.0);
    rhi::Gvtcier_Rhi_Clear(DepthBit);
    {
        // 阴影渲染统一剔正面（cullMode=Front）：只写物体背面，
        // 这样物体自身的正面不会把自己的表面判成阴影，是标准的阴影偏移替代手段
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Front;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    depthShader_.Gvtcier_Shader_Use();
    depthShader_.Gvtcier_Shader_SetMat4("uLightVP", volLightVp_.m);
    depthShader_.Gvtcier_Shader_SetVec3("uShadowAnchor", 0.0f, 0.0f, 0.0f);
    scene.ForEachObject([&](const Object& o, GvtId<Object>) {
        Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
        if (!o.visible || !o.castShadow || !om) return;
        depthShader_.Gvtcier_Shader_SetMat4("uModel", modelMatrix(o).m);
        om->draw();
    });
    if (scene.hasSkinned() &&
        scene.skinnedBoneMats.size() >= scene.skinnedMesh().bones.size() * 16) {
        // 蒙皮网格单独用带骨骼的深度着色器；uModel 传单位阵，位置完全由骨骼矩阵决定
        depthSkinnedShader_.Gvtcier_Shader_Use();
        depthSkinnedShader_.Gvtcier_Shader_SetMat4("uLightVP", volLightVp_.m);
        depthSkinnedShader_.Gvtcier_Shader_SetVec3("uShadowAnchor", 0.0f, 0.0f, 0.0f);
        Mat4 sm = Mat4::identity();
        depthSkinnedShader_.Gvtcier_Shader_SetMat4("uModel", sm.m);
        depthSkinnedShader_.Gvtcier_Shader_SetMat4Array("uBones", scene.skinnedBoneMats.data(),
                                    (int)scene.skinnedMesh().bones.size());
        scene.skinnedMesh().mesh.draw();
    }
    volShadow_.unbind(viewW_, viewH_);
    hdrFbo_.bind();
    rhi::Gvtcier_Rhi_Viewport(0, 0, viewW_, viewH_);
    {
        // 画完阴影图后把状态恢复成主几何 pass 的常规值，避免影响后续 pass
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
}

// 太阳（平行光）级联阴影贴图渲染，共 4 个级联，写进 sunShadow_ 的四层深度数组。
//
// 为什么要级联：平行光的正交投影精度与覆盖范围成反比。一张图同时罩住近处 1 米和远处 120 米，
// 每个阴影纹素会大到覆盖几十厘米，近处阴影全是锯齿。所以把可视距离切成 4 段，
// 每段单独用一张正交图，近段精度高、远段精度低，这就是 CSM。
//
// 级联切分（csmSplits_）：
//   t = (i+1)/4 是这一段在远平面上的归一化位置
//   对数切分 logZ = near * (far/near)^t  —— 符合人眼对距离的对数感知，近处分得细
//   线性切分 linZ = near + (far-near)*t —— 均匀切分
//   两者按 lambda 混合，lambda=0.6 偏对数侧：近处精度更好，远处也不会切得太碎，
//   这是 PSSM 的经典公式，0.6 是常见的折中值
//   阴影最大距离取 min(相机远平面, 120)，即 120 米外的物体不再投影，
//   因为远处阴影在屏幕上本来就只有几像素，看不到，但会白白吃掉整张阴影图精度
//
// 每个级联的正交图做法：
//   - 光照空间三个轴：zAxis=光线方向，x/yAxis 由任意 up 叉乘得到（保证正交）
//   - 视锥在该距离的包围球半径 = ff * cornerFactor，cornerFactor 是视锥四角的统一缩放系数；
//     用包围球而不是紧贴视锥的 AABB，是为了让阴影图在相机旋转时尺寸稳定不抖动
//   - 半径乘 size/(size-2)：预留 1 个纹素的边，供 PCF 采样时不会取到图外
//   - 半径向上取整到纹素大小的整数倍，再用纹素对齐相机中心（texel snapping），
//     否则相机一动，阴影边缘就会逐个纹素跳动（swimming）
//   - 近远平面由所有投影物体的光照空间 z 范围推出（tmin/tmax），再前后各加 1，
//     最后额外把 far 拉长 pancake = max(radius*0.25, 1)，让几何体背面也能投到影
//
// 输出：csmVP_[4] 每个级联的视图投影、csmTexel_[4] 纹素大小、csmRange_[4] 深度范围、csmAnchor_。
// 前置条件：opt.showShadow 为真且阴影图确实有 4 层。
void Renderer::sunShadowPass(Scene& scene, const Camera& cam, const Vec3& sunDirW, const RenderOptions& opt) {
    if (!opt.showShadow || sunShadow_.layers() < 4) return;
    if (sunDirW.length() < 1e-8f) return;

    float nearZ = 0.05f;
    // 120 米是阴影可视距离上限：再远的阴影在屏幕上不到几像素，投了也看不出来
    float shadowMaxDist = std::fmin(cam.farPlane, 120.0f);
    float farZ = shadowMaxDist;
    // 0.6 是 PSSM 混合系数，偏对数切分，近处精度优先
    float lambda = 0.6f;
    // 4 个级联的分界距离，csmSplits_[i] 是第 i 段能覆盖到的最远距离
    for (int i = 0; i < 4; ++i) {
        float t = (float)(i + 1) / 4.0f;
        float logZ = nearZ * std::pow(farZ / nearZ, t);
        float linZ = nearZ + (farZ - nearZ) * t;
        csmSplits_[i] = logZ * lambda + linZ * (1.0f - lambda);
    }

    float aspect = (float)viewW_ / (float)viewH_;
    Vec3 L = sunDirW.normalized();

    // 构造光照空间基：z 轴指向太阳，x/y 由 up 叉乘得到。
    // 光线几乎竖直时世界 Y 不能当 up（叉乘会退化），换成 X 轴
    Vec3 up = (std::fabs(L.y) > 0.99f) ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    Vec3 zAxis = L;
    Vec3 xAxis = up.cross(zAxis).normalized();
    Vec3 yAxis = zAxis.cross(xAxis).normalized();

    // 视锥四角的统一缩放系数：视锥在距离 1 处的半高是 tanV、半宽是 tanH，
    // 四角到中心的距离就是 sqrt(1 + tanH² + tanV²)，所以距离 d 的切面外接球半径是 d * 这个系数
    double tanH0 = std::tan(deg2rad(cam.fov) * 0.5);
    double tanV0 = tanH0 / aspect;
    double cornerFactor = std::sqrt(1.0 + tanH0 * tanH0 + tanV0 * tanV0);

    Mat4 mainVp = cam.projMatrix((float)viewW_ / (float)viewH_) * cam.viewMatrix();
    // 从视图投影矩阵的列组合出 6 个视锥平面（Gribb-Hartmann 提取法），
    // 系数取出后归一化，方便后面用距离做判断
    auto fplane = [](double a, double b, double cc, double d) {
        double len = std::sqrt(a * a + b * b + cc * cc);
        if (len < 1e-20) len = 1.0;
        Plane3 p = { Vec3(-a / len, -b / len, -cc / len), d / len };
        return p;
    };
    const double* mv = mainVp.m;
    Plane3 fplanes[6] = {
        fplane(mv[3] + mv[2], mv[7] + mv[6], mv[11] + mv[10], mv[15] + mv[14]),
        fplane(mv[3] - mv[2], mv[7] - mv[6], mv[11] - mv[10], mv[15] - mv[14]),
        fplane(mv[3] + mv[0], mv[7] + mv[4], mv[11] + mv[8], mv[15] + mv[12]),
        fplane(mv[3] - mv[0], mv[7] - mv[4], mv[11] - mv[8], mv[15] - mv[12]),
        fplane(mv[3] + mv[1], mv[7] + mv[5], mv[11] + mv[9], mv[15] + mv[13]),
        fplane(mv[3] - mv[1], mv[7] - mv[5], mv[11] - mv[9], mv[15] - mv[13]),
    };

    struct Caster { Mesh* mesh; Mat4 model; Vec3 center; Vec3 half; };
    std::vector<Caster> casters;
    // 预筛所有投影物体，只保留可能影响阴影的那部分：
    //   - 不可见或不投影的跳过
    //   - 超出阴影最大距离 8 米的跳过（8 米是留的余量，防止物体正好在边界外但影子伸进可见区）
    //   - 视锥测试时包围盒外加 8 米的余量，理由同上——物体本身在视锥外，影子可能落在视锥内
    // 之后每个级联都会复用这份列表，避免重复遍历场景
    scene.ForEachObject([&](const Object& o, GvtId<Object>) {
        Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
        if (!o.visible || !o.castShadow || !om) return;
        AABB3 ob = o.Gvtcier_Object_Bounds(scene.Gvtcier_Scene_MeshHalfExtents(o.mesh));
        Vec3 dc = ob.center - cam.pos;
        float reach = (float)ob.half.length();
        if (dc.length() - reach > shadowMaxDist + 8.0f) return;
        AABB3 eb = ob;
        eb.half = eb.half + Vec3(8.0, 8.0, 8.0);
        if (aabbOutsideFrustum(eb, fplanes, 6)) return;
        casters.push_back({ om, modelMatrix(o), ob.center, ob.half });
    });

    asmCount_ = 0;
    {
        float asmReach = csmSplits_[1];
        // 先按距离把近级候选排好序，再取前 kAsmSlots 个。
        // 直接按场景遍历顺序分配的话，槽位会被先遇到的物体占满，
        // 结果视线中心最近的物体反而拿不到子相机，退化成粗阴影。
        struct AsmPick { float dist; const Caster* caster; };
        std::vector<AsmPick> picks;
        for (const Caster& cs : casters) {
            Vec3 rel = cs.center - cam.pos;
            float dist = rel.length();
            if (dist > asmReach) continue;
            if (cs.half.length() > kAsmMaxRadius) continue;
            picks.push_back({ dist, &cs });
        }
        std::sort(picks.begin(), picks.end(),
                  [](const AsmPick& a, const AsmPick& b) { return a.dist < b.dist; });

        for (size_t k = 0; k < picks.size() && asmCount_ < kAsmSlots; ++k) {
            const Caster& cs = *picks[k].caster;
            float extX = std::fabs(cs.half.x * xAxis.x) + std::fabs(cs.half.y * xAxis.y) + std::fabs(cs.half.z * xAxis.z);
            float extY = std::fabs(cs.half.x * yAxis.x) + std::fabs(cs.half.y * yAxis.y) + std::fabs(cs.half.z * yAxis.z);
            float extZ = std::fabs(cs.half.x * zAxis.x) + std::fabs(cs.half.y * zAxis.y) + std::fabs(cs.half.z * zAxis.z);
            float rad = std::fmax(extX, extY) * 1.35f + 0.25f;
            float zLo = -(extZ + 2.0f);
            float zHi = 2.5f + extZ + 4.0f;
            float farD = zHi - zLo;
            Vec3 eyeRel = zAxis * zHi;
            Mat4 lv = Mat4::lookAt(eyeRel, Vec3(0.0f, 0.0f, 0.0f), yAxis);
            Mat4 lp = Mat4::ortho(-rad, rad, -rad, rad, 0.0f, farD, Conventions::active());
            AsmCam& ac = asmCams_[asmCount_];
            ac.vp = lp * lv;
            ac.center = cs.center;
            ac.radius = rad;
            ac.judge = rad + extZ * 2.0f + 1.0f;
            ac.zLo = zLo;
            ac.zHi = zHi;
            ac.range = farD;
            ac.texel = (2.0f * rad) / (float)asmShadow_.size();
            ++asmCount_;
        }
    }

    for (int c = 0; c < 4; ++c) {
        // 第 c 段的远距离，乘以 cornerFactor 得到该距离视锥切面的外接球半径
        float ff = csmSplits_[c];
        Vec3 center = cam.pos;
        // 阴影对齐锚点用相机位置，着色器里据此做世界坐标到阴影空间的稳定性修正
        csmAnchor_ = cam.pos;
        float radius = (float)(ff * cornerFactor);
        // 乘 size/(size-2)：留出 1 纹素边距，PCF 采样不会采到图外
        radius *= (float)sunShadow_.size() / (float)(sunShadow_.size() - 2);
        radius = std::fmax(radius, 0.5f);

        // tw 是当前阴影图一个纹素在世界空间里的边长
        float tw = (float)(2.0 * radius) / (float)sunShadow_.size();
        // 半径向上取整到纹素整数倍，这样后面纹素对齐中心时不会错位
        radius = std::ceil(radius / tw) * tw;
        tw = (float)(2.0 * radius) / (float)sunShadow_.size();

        // 纹素对齐（texel snapping）：把相机中心投影到光照空间的 x/y 平面上，
        // 向下取整到纹素整数倍，得到相对相机的偏移 cRel。
        // 这样相机轻微移动时阴影图内容不会整体漂移，消除阴影边缘“游泳”的现象
        double cx = xAxis.dot(center);
        double cy = yAxis.dot(center);
        cx = std::floor(cx / (double)tw) * (double)tw - cx;
        cy = std::floor(cy / (double)tw) * (double)tw - cy;
        Vec3 cRel((float)cx, (float)cy, 0.0f);

        // 统计所有投影物体在光照空间 z 方向的覆盖范围，用来定正交相机近远平面
        float tmin = -radius;
        float tmax = radius;
        for (const Caster& cs : casters) {
            Vec3 rel = cs.center - cam.pos;
            // 半长向量在三个光照轴上的投影长度之和，就是包围盒在该轴上的半宽（OBB 投影到 AABB）
            float extX = std::fabs(cs.half.x * xAxis.x) + std::fabs(cs.half.y * xAxis.y) + std::fabs(cs.half.z * xAxis.z);
            float extY = std::fabs(cs.half.x * yAxis.x) + std::fabs(cs.half.y * yAxis.y) + std::fabs(cs.half.z * yAxis.z);
            float extZ = std::fabs(cs.half.x * zAxis.x) + std::fabs(cs.half.y * zAxis.y) + std::fabs(cs.half.z * zAxis.z);
            float lx = (float)xAxis.dot(rel) - cRel.x;
            float ly = (float)yAxis.dot(rel) - cRel.y;
            // 在光照空间的横向范围完全落在本段阴影图之外，投不到这一层，跳过
            if (std::fabs(lx) > extX + radius || std::fabs(ly) > extY + radius) continue;
            float lz = (float)zAxis.dot(rel);
            tmin = std::fmin(tmin, lz - extZ);
            tmax = std::fmax(tmax, lz + extZ);
        }
        // 前后各放 1 米余量，防止贴面几何被近/远平面裁掉
        tmin -= 1.0f;
        tmax += 1.0f;

        // pancake：额外拉长的远平面厚度，取投影范围的 1/4（最小 1 米）。
        // 作用是让“背对光源的背面”也能落进深度范围，配合剔正面渲染减少漏光
        float pancake = std::fmax(radius * 0.25f, 1.0f);
        float farD = (tmax - tmin) + pancake;
        // 眼睛放在最远端外侧，朝 cRel 看，这样 -z 方向就是光照方向
        Vec3 eyeRel = cRel + zAxis * (tmax + pancake);
        Mat4 lv = Mat4::lookAt(eyeRel, cRel, yAxis);
        Mat4 lp = Mat4::ortho(-radius, radius, -radius, radius,
                              0.0f, farD, Conventions::active());
        csmVP_[c] = lp * lv;
        csmTexel_[c] = tw;
        csmRange_[c] = farD;

        // 渲染第 c 层。和体积阴影一样剔正面写背面，避免自阴影
        sunShadow_.bindLayer(c);
        {
            // 剔正面：只写背面深度，防止物体自身正面把自己判成阴影
            PassState st;
            st.depthTest = true;
            st.depthWrite = true;
            st.depthFunc = Greater;
            st.cull = true;
            st.cullMode = Front;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = Fill;
            applyPassState(st);
        }
        depthShader_.Gvtcier_Shader_Use();
        depthShader_.Gvtcier_Shader_SetMat4("uLightVP", csmVP_[c].m);
        depthShader_.Gvtcier_Shader_SetVec3("uShadowAnchor", (float)csmAnchor_.x, (float)csmAnchor_.y, (float)csmAnchor_.z);
        for (const Caster& cs : casters) {
            // 蒙皮网格不参与太阳级联阴影（这里只画静态网格的 model 矩阵）
            depthShader_.Gvtcier_Shader_SetMat4("uModel", cs.model.m);
            cs.mesh->draw();
        }
    }
    sunShadow_.unbind(viewW_, viewH_);

    if (asmCount_ > 0) {
        for (int i = 0; i < asmCount_; ++i) {
            AsmCam& ac = asmCams_[i];
            float zMin = ac.zLo;
            float zMax = ac.zHi;
            for (const Caster& cs : casters) {
                Vec3 rel = cs.center - ac.center;
                float ex = std::fabs(cs.half.x * xAxis.x) + std::fabs(cs.half.y * xAxis.y) + std::fabs(cs.half.z * xAxis.z);
                float ey = std::fabs(cs.half.x * yAxis.x) + std::fabs(cs.half.y * yAxis.y) + std::fabs(cs.half.z * yAxis.z);
                float ez = std::fabs(cs.half.x * zAxis.x) + std::fabs(cs.half.y * zAxis.y) + std::fabs(cs.half.z * zAxis.z);
                float lx = xAxis.dot(rel);
                float ly = yAxis.dot(rel);
                float lz = zAxis.dot(rel);
                if (std::fabs(lx) > ex + ac.radius) continue;
                if (std::fabs(ly) > ey + ac.radius) continue;
                zMin = std::fmin(zMin, lz - ez);
                zMax = std::fmax(zMax, lz + ez);
            }
            zMin -= 1.0f;
            zMax += 1.0f;
            ac.zLo = zMin;
            ac.zHi = zMax;
            ac.range = zMax - zMin;
            Mat4 lv = Mat4::lookAt(zAxis * zMax, Vec3(0.0f, 0.0f, 0.0f), yAxis);
            Mat4 lp = Mat4::ortho(-ac.radius, ac.radius, -ac.radius, ac.radius, 0.0f, ac.range, Conventions::active());
            ac.vp = lp * lv;
            asmShadow_.bindLayer(i);
            {
                PassState st;
                st.depthTest = true;
                st.depthWrite = true;
                st.depthFunc = Greater;
                st.cull = true;
                st.cullMode = Front;
                st.frontFace = Ccw;
                st.blend = false;
                st.scissor = false;
                st.polygonMode = Fill;
                applyPassState(st);
            }
            depthShader_.Gvtcier_Shader_Use();
            depthShader_.Gvtcier_Shader_SetMat4("uLightVP", ac.vp.m);
            depthShader_.Gvtcier_Shader_SetVec3("uShadowAnchor", ac.center.x, ac.center.y, ac.center.z);
            for (const Caster& cs : casters) {
                Vec3 rel = cs.center - ac.center;
                float ex = std::fabs(cs.half.x * xAxis.x) + std::fabs(cs.half.y * xAxis.y) + std::fabs(cs.half.z * xAxis.z);
                float ey = std::fabs(cs.half.x * yAxis.x) + std::fabs(cs.half.y * yAxis.y) + std::fabs(cs.half.z * yAxis.z);
                float ez = std::fabs(cs.half.x * zAxis.x) + std::fabs(cs.half.y * zAxis.y) + std::fabs(cs.half.z * zAxis.z);
                float lx = xAxis.dot(rel);
                float ly = yAxis.dot(rel);
                float lz = zAxis.dot(rel);
                if (std::fabs(lx) > ex + ac.radius) continue;
                if (std::fabs(ly) > ey + ac.radius) continue;
                if (lz - ez > ac.zHi || lz + ez < ac.zLo) continue;
                depthShader_.Gvtcier_Shader_SetMat4("uModel", cs.model.m);
                cs.mesh->draw();
            }
        }
        asmShadow_.unbind(viewW_, viewH_);
    }
}

// 体积光/体积雾主 pass：在低分辨率缓冲上做 ray march，然后做两趟分离式模糊，结果供 compose 合成。
//
// 流程：
//   1. 半分辨率两张 Rgba16F 缓冲（volTex_[0]/[1]）按屏幕尺寸变化重建
//   2. volShader 对每个像素沿视线步进，用深度图决定步进终点、用体积阴影图决定该点是否被遮挡，
//      叠加相位函数（Mie 散射的前向散射）与 3D 噪声，得到半分辨率的体积光颜色
//   3. 横向模糊到 volTex_[1]，再纵向模糊回 volTex_[0]，两趟分开做节省采样数（分离式高斯）
//   4. 恢复主视口与主几何 pass 的状态，并递增 volFrame_ 供着色器的抖动噪声变化
//
// 两档参数由 fogLevel_ 决定：
//   fogLevel_ == 3（高档体积雾）：密度固定 0.070、光强放宽 6 倍、步数 32、最远 30 米、
//     噪声更大更粗、相位 g=0.55（前向散射更集中）、模式 2
//   fogLevel_ == 2（普通体积光）：密度由场景 fogDensity 推出、步数 48、最远 120 米、
//     相位 g=0.30、模式 3
//   值越大散射越集中在前方（迎着太阳看更亮），g=0 是均匀散射。
//   最远距离短会更省，但雾在远处会被截断；高档把距离压到 30 米就是因为密度大，远处本来也看不到。
void Renderer::volumetricPass(Scene& scene, const Camera& cam, const RenderOptions& opt) {
    if (volShader_.Gvtcier_Shader_Id() == 0 || volBlurShader_.Gvtcier_Shader_Id() == 0) return;
    if (!volLightValid_) return;
    int sw = 0, sh = 0;
    renderRes(sw, sh, opt);
    // 体积光是低频效果，用半分辨率就够，模糊后上采样也看不出明显差别，性能却省 4 倍
    int vw = sw / 2 > 0 ? sw / 2 : 1;
    int vh = sh / 2 > 0 ? sh / 2 : 1;
    if (volFbo_[0] == 0 || volW_ != vw || volH_ != vh) {
        // 尺寸变了就重建两张半分辨率缓冲
        for (int i = 0; i < 2; ++i) {
            if (volFbo_[i]) { rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &volFbo_[i]); volFbo_[i] = 0; }
            if (volTex_[i]) { rhi::Gvtcier_Rhi_DeleteTextures(1, &volTex_[i]); volTex_[i] = 0; }
            rhi::Gvtcier_Rhi_GenTextures(1, &volTex_[i]);
            rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, volTex_[i]);
            rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba16F, vw, vh, Rgba, Float, nullptr);
            rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
            rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
            rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
            rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
            rhi::Gvtcier_Rhi_GenFramebuffers(1, &volFbo_[i]);
            rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, volFbo_[i]);
            rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment0, Tex2D, volTex_[i], 0);
            rhi::Gvtcier_Rhi_DrawBuffer(ColorAttachment0);
        }
        rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, hdrFbo_.fbo());
        volW_ = vw; volH_ = vh;
    }

    Vec3 sunDirW(-scene.light.dir.x, -scene.light.dir.y, -scene.light.dir.z);
    float sl = sunDirW.length();
    if (sl > 1e-6f) sunDirW = sunDirW / sl;

    Mat4 proj = cam.projMatrix((float)opt.width / (float)opt.height);
    Mat4 view = cam.viewMatrix();
    Mat4 invVP = (proj * view).inverse();

    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, volFbo_[0]);
    rhi::Gvtcier_Rhi_Viewport(0, 0, vw, vh);
    {
        // 体积光 pass 不碰深度：它是全屏后处理，深度只作为纹理读取
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    volShader_.Gvtcier_Shader_Use();
    volShader_.Gvtcier_Shader_SetInt("uDepth", TexSlot::Scene);
    volShader_.Gvtcier_Shader_SetInt("uShadow", TexSlot::ShadowMap);
    volShader_.Gvtcier_Shader_SetInt("uNoise", TexSlot::Noise);
    volShader_.Gvtcier_Shader_SetMat4("uInvVP", invVP.m);
    volShader_.Gvtcier_Shader_SetMat4("uLightVP", volLightVp_.m);
    volShader_.Gvtcier_Shader_SetVec3("uCamPos", cam.pos.x, cam.pos.y, cam.pos.z);
    volShader_.Gvtcier_Shader_SetVec3("uSunDir", sunDirW.x, sunDirW.y, sunDirW.z);
    bool volFogHigh = (fogLevel_ >= 3);
    // 高档体积雾密度固定 0.070；普通档由场景雾密度推出，乘 3 是把场景的 0~1 参数放大到体渲染的量级
    float volDensity = volFogHigh ? 0.070f : (0.030f + scene.fogDensity * 3.0f);
    // 太阳光在体积里的强度：高档默认 6 倍以保证浓雾里也看得见光柱；
    // 普通档开启体积光时 3 倍，否则给 1.2 倍当作恒定环境亮度
    float volSunMul = volFogHigh ? scene.volStrength * 6.0f
                                 : (scene.volumeLightEnabled ? scene.volStrength * 3.0f : 1.2f);
    volShader_.Gvtcier_Shader_SetVec3("uSunColor",
        scene.light.color.x * volSunMul,
        scene.light.color.y * volSunMul,
        scene.light.color.z * volSunMul);
    volShader_.Gvtcier_Shader_SetVec3("uAmbient",
        scene.ambientColor.x * scene.ambientIntensity * 6.0f,
        scene.ambientColor.y * scene.ambientIntensity * 6.0f,
        scene.ambientColor.z * scene.ambientIntensity * 6.0f);
    volShader_.Gvtcier_Shader_SetFloat("uDensity", volDensity);
    volShader_.Gvtcier_Shader_SetFloat("uExtinction", volFogHigh ? 1.0f : 0.6f);
    volShader_.Gvtcier_Shader_SetFloat("uNoiseAmt", volFogHigh ? 0.60f : 0.75f);
    volShader_.Gvtcier_Shader_SetFloat("uNoiseScale", volFogHigh ? 0.30f : 0.15f);
    volShader_.Gvtcier_Shader_SetFloat("uPhaseG", volFogHigh ? 0.55f : 0.30f);
    volShader_.Gvtcier_Shader_SetFloat("uMaxDist", volFogHigh ? 30.0f : 120.0f);
    volShader_.Gvtcier_Shader_SetFloat("uFrame", (float)volFrame_);
    volShader_.Gvtcier_Shader_SetFloat("uShadowBias", 0.0010f);
    volShader_.Gvtcier_Shader_SetFloat("uShadowTexel", 1.0f / (float)volShadow_.size());
    // 步数直接决定体积光的细腻程度与开销：高档雾只需要 32 步（距离短），普通档 48 步
    volShader_.Gvtcier_Shader_SetInt("uSteps", volFogHigh ? 32 : 48);
    volShader_.Gvtcier_Shader_SetInt("uEnable", 1);
    volShader_.Gvtcier_Shader_SetInt("uMode", volFogHigh ? 2 : 3);
    volShader_.Gvtcier_Shader_SetFloat("uDebugRed", volDebugRed_ ? 1.0f : 0.0f);
    volShader_.Gvtcier_Shader_SetInt("uCloudEnable", 0);
    volShader_.Gvtcier_Shader_SetFloat("uCloudAlt", 1100.0f);
    volShader_.Gvtcier_Shader_SetFloat("uCloudH", 80.0f);
    volShader_.Gvtcier_Shader_SetFloat("uCloudDensity", 0.030f);
    volShader_.Gvtcier_Shader_SetFloat("uCloudScale", 0.003f);
    volShader_.Gvtcier_Shader_SetFloat("uCloudCover", 0.48f);
    volShader_.Gvtcier_Shader_SetFloat("uCloudBright", 1.7f);
    volShader_.Gvtcier_Shader_SetFloat("uCloudSpeed", 0.002f);
    volShader_.Gvtcier_Shader_SetInt("uCloudSteps", 24);
    volShader_.Gvtcier_Shader_SetVec3("uCloudSun",
        scene.light.color.x * 2.2f,
        scene.light.color.y * 2.2f,
        scene.light.color.z * 2.2f);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, hdrFbo_.depthTex());
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::ShadowMap, Tex2D, volShadow_.depthTex());
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Noise, Tex3D, volNoiseTex_);
    fullscreenQuad_.draw();

    // 分离式模糊第一趟：横向（uDir 只给 x 方向），先清空目标缓冲
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, volFbo_[1]);
    rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    rhi::Gvtcier_Rhi_Clear(ColorBit);
    volBlurShader_.Gvtcier_Shader_Use();
    volBlurShader_.Gvtcier_Shader_SetInt("uTex", 0);
    volBlurShader_.Gvtcier_Shader_SetVec2("uDir", 1.0f / (float)vw, 0.0f);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, volTex_[0]);
    fullscreenQuad_.draw();

    // 分离式模糊第二趟：纵向，从第一趟的结果模糊回 volTex_[0]
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, volFbo_[0]);
    rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    rhi::Gvtcier_Rhi_Clear(ColorBit);
    volBlurShader_.Gvtcier_Shader_SetVec2("uDir", 0.0f, 1.0f / (float)vh);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, volTex_[1]);
    fullscreenQuad_.draw();

    rhi::Gvtcier_Rhi_Viewport(0, 0, viewW_, viewH_);
    {
        // 体积光结束后恢复主几何状态，后续的透明几何 pass 依赖它
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    // 帧计数递增：着色器里的 uFrame 用来做时变抖动，避免 ray march 出现规律条纹
    ++volFrame_;
}

// 重建天空渐变查找纹理（宽 256、高 1 的一条渐变带）。
// 上半球三段：天顶深蓝 → 中段蓝 → 地平线浅蓝；下半球：地平线灰 → 天底深灰。
// 常规游戏把天空盒横向拉伸成椭圆让天看起来很大，这里的压扁在 sky.vert 里做（只缩绘制高度），
// 渐变本身按视线方向的高度 h 查表，不受几何压扁影响。
void Renderer::buildSkyLut(const Vec3& skyColor) {
    static bool built = false;
    if (built) return;
    built = true;
    const int N = 256;
    unsigned char px[N * 4];
    const Vec3 kZenith(0.10f, 0.22f, 0.48f);
    const Vec3 kMid(0.30f, 0.50f, 0.78f);
    const Vec3 kHorizonUp(0.55f, 0.70f, 0.88f);
    const Vec3 kHorizonDown(0.45f, 0.46f, 0.48f);
    const Vec3 kNadir(0.16f, 0.17f, 0.19f);
    for (int i = 0; i < N; ++i) {
        float h = (float)i / (float)(N - 1) * 2.0f - 1.0f;
        Vec3 c;
        if (h >= 0.0f) {
            float t = h * h * (3.0f - 2.0f * h);
            if (t < 0.5f) {
                float k = t / 0.5f;
                c = kHorizonUp + (kMid - kHorizonUp) * k;
            } else {
                float k = (t - 0.5f) / 0.5f;
                c = kMid + (kZenith - kMid) * k;
            }
        } else {
            float t = (-h) * (-h) * (3.0f - 2.0f * (-h));
            c = kHorizonDown + (kNadir - kHorizonDown) * t;
        }
        px[i * 4 + 0] = (unsigned char)(std::fmax(0.0f, std::fmin(1.0f, c.x)) * 255.0f + 0.5f);
        px[i * 4 + 1] = (unsigned char)(std::fmax(0.0f, std::fmin(1.0f, c.y)) * 255.0f + 0.5f);
        px[i * 4 + 2] = (unsigned char)(std::fmax(0.0f, std::fmin(1.0f, c.z)) * 255.0f + 0.5f);
        px[i * 4 + 3] = 255;
    }
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, skyGradLut_);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba8, N, 1, Rgba, UnsignedByte, px);
}

// 设置 3D 内部渲染分辨率（与窗口尺寸分离，用于分档渲染或超分）。
// 传 0 表示跟随窗口尺寸（renderRes 里会回退到 opt.width/height）。
void Renderer::Gvtcier_Render_SetRatio(int width, int height) {
    renderW_ = width > 0 ? width : 0;
    renderH_ = height > 0 ? height : 0;
}

// 读当前设置的 3D 渲染分辨率，0 表示跟随窗口。
void Renderer::Gvtcier_Render_GetRatio(int* width, int* height) const {
    if (width) *width = renderW_;
    if (height) *height = renderH_;
}

// 阴影调试探针：读屏幕上指定像素（由 Gvtcier_Shadow_ProbeCursor 给出）的 G-Buffer 数据，
// 交给阴影调参面板显示。只在调试层数 >= 9 时才会被调用，正常渲染不走这里。
//
// 读回的 4 个分量约定（由延迟光照/阴影调配着色器写出）：
//   fb[0]  级联索引，编码为 (casc - 0.5)/3，所以解码是 fb[0]*3 + 0.5
//   fb[1]  量化后的深度值 qz
//   fb[2]  深度差 dc（当前像素深度与阴影图深度之差）
//   fb[3]  阴影差异，编码为 diff*500 + 0.5，解码是 (fb[3]-0.5)/500
// 注意屏幕坐标要翻转 Y：GL 的 (0,0) 在左下，而 UI 坐标原点在左上。
// 读取单像素是同步的，会打断 GPU 流水，所以只在调试面板打开时用。
void Renderer::shadowProbePass(const Camera& cam) {
    (void)cam;
    ShadowProbe& pb = Gvtcier_Shadow_Probe();
    pb.valid = 0.0f;
    const int* cur = Gvtcier_Shadow_ProbeCursor();
    int mx = cur[0];
    int my = cur[1];
    if (mx < 0 || my < 0 || mx >= viewW_ || my >= viewH_) return;
    float fb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int readY = viewH_ - 1 - my;
    rhi::Gvtcier_Rhi_ReadPixels(mx, readY, 1, 1, Rgba, Float, fb);
    pb.valid = 1.0f;
    pb.casc = fb[0] * 3.0f + 0.5f;
    pb.qz = fb[1];
    pb.dc = fb[2];
    pb.diff = (fb[3] - 0.5f) / 500.0f;
    Gvtcier_Shadow_Tuning_RefreshProbe();
}

// 给主着色器/延迟光照着色器上传级联阴影的全部 uniform。
// csmOn 为 false 时只传开关并提前返回，着色器走无阴影分支，省掉其余上传。
// 上传内容：
//   uShadowTexel  单个纹素在世界空间的边长，PCF 采样步长用
//   uFrameNoise   每帧变化的抖动偏移（帧号 % 64 再乘 5.588238，即黄金角弧度），
//                 让 PCF 的采样旋转角度逐帧变化，把噪点变成可被人眼平均掉的细微抖动
//   uShadowAnchor 阴影图对齐锚点（相机位置），着色器据此修正采样坐标的抖动
//   uCSMVP[0..3]  四个级联的光照视图投影矩阵
//   uCSMSplits    各级联的可视距离，着色器据此选层
//   uCSMTexel     各级联纹素大小，用于世界空间到阴影 uv 的换算
//   uCSMRange     各级联深度范围，用于深度归一化
void Renderer::uploadCsmUniforms(Shader& sh, bool csmOn) {
    sh.Gvtcier_Shader_SetInt("uCSMOn", csmOn ? 1 : 0);
    sh.Gvtcier_Shader_SetInt("uPS0", TexSlot::PointShadowCube);
    sh.Gvtcier_Shader_SetInt("uPS1", TexSlot::PointShadowCube2);
    sh.Gvtcier_Shader_SetInt("uPS2", TexSlot::PointShadowCube3);
    sh.Gvtcier_Shader_SetInt("uPS3", TexSlot::PointShadowCube4);
    if (!csmOn) return;
    sh.Gvtcier_Shader_SetInt("uCSM", TexSlot::CSMShadow);
    sh.Gvtcier_Shader_SetFloat("uShadowTexel", 1.0f / (float)sunShadow_.size());
    sh.Gvtcier_Shader_SetFloat("uFrameNoise", (float)((++csmNoiseFrame_) % 64) * 5.588238f);
    sh.Gvtcier_Shader_SetVec3("uShadowAnchor", (float)csmAnchor_.x, (float)csmAnchor_.y, (float)csmAnchor_.z);
    for (int c = 0; c < 4; ++c) {
        char cn[64];
        snprintf(cn, sizeof(cn), "uCSMVP[%d]", c);
        sh.Gvtcier_Shader_SetMat4(cn, csmVP_[c].m);
    }
    sh.Gvtcier_Shader_SetVec4("uCSMSplits", csmSplits_[0], csmSplits_[1], csmSplits_[2], csmSplits_[3]);
    sh.Gvtcier_Shader_SetVec4("uCSMTexel", csmTexel_[0], csmTexel_[1], csmTexel_[2], csmTexel_[3]);
    sh.Gvtcier_Shader_SetVec4("uCSMRange", csmRange_[0], csmRange_[1], csmRange_[2], csmRange_[3]);
}

// 上传额外平行光的数组 uniform，名字按 uDirLights[i] / uDirLightCols[i] 拼出来，
// 数量不超过全局上限 maxDirLights（调用方已经截断过）。
// 方向向量 vec4 的 w 位固定填 1，与主光方向保持同一约定。
void Renderer::uploadDirLightUniforms(Shader& sh, const std::vector<Vec4>& dirs, const std::vector<Vec4>& cols) {
    for (int dk = 0; dk < (int)dirs.size(); ++dk) {
        char dln[64];
        snprintf(dln, sizeof(dln), "uDirLights[%d]", dk);
        sh.Gvtcier_Shader_SetVec4(dln, dirs[dk].x, dirs[dk].y, dirs[dk].z, dirs[dk].w);
        snprintf(dln, sizeof(dln), "uDirLightCols[%d]", dk);
        sh.Gvtcier_Shader_SetVec4(dln, cols[dk].x, cols[dk].y, cols[dk].z, cols[dk].w);
    }
}

void Renderer::uploadAsmUniforms(Shader& sh, bool on) {
    sh.Gvtcier_Shader_SetInt("uAsm", TexSlot::AsmShadow);
    sh.Gvtcier_Shader_SetInt("uAsmCount", on ? asmCount_ : 0);
    for (int i = 0; i < asmCount_ && on; ++i) {
        char n[64];
        snprintf(n, sizeof(n), "uAsmVP[%d]", i);
        sh.Gvtcier_Shader_SetMat4(n, asmCams_[i].vp.m);
        snprintf(n, sizeof(n), "uAsmInfo[%d]", i);
        sh.Gvtcier_Shader_SetVec4(n, asmCams_[i].center.x, asmCams_[i].center.y, asmCams_[i].center.z, asmCams_[i].judge);
        snprintf(n, sizeof(n), "uAsmMeta[%d]", i);
        // z 分量是这张图集在 UV 空间里一个纹素的边长，着色器做 3x3 偏移靠它，
        // 硬编 1/256 的话以后改分辨率会静默出错
        float uvTexel = 1.0f / (float)asmShadow_.size();
        sh.Gvtcier_Shader_SetVec4(n, asmCams_[i].texel, asmCams_[i].range, uvTexel, 0.0f);
    }
}

// 画 2D 画布队列（canvasItems_），纯 2D 模式下的主绘制入口。
// 与 flushQueue 的区别：这里不做文字缓存、不支持图片、也不区分引擎/游戏队列，
// 只处理矩形和带黑描边的文字，实现更简单。
// 流程：
//   1. 光栅状态切成 2D：关深度、开 alpha 混合、不裁剪
//   2. 按 z 稳定排序，保证同一 z 的图元保持入队顺序
//   3. 顺序遍历，矩形与文字分别攒进 rverts/tverts 两个顶点数组，
//      遇到类型切换就把当前这批一次性提交（合批，减少 draw call）
//   4. 文字用 8 个偏移方向叠一遍黑色描边再画本色，保证任何背景下都能看清
// 调用后清空 canvasItems_。
void Renderer::renderCanvasItems2D() {
    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = true;
        st.blendSrc = SrcAlpha;
        st.blendDst = OneMinusSrcAlpha;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    if (!canvasItems_.empty()) {
        std::stable_sort(canvasItems_.begin(), canvasItems_.end(),
                         [](const CanvasPrim& a, const CanvasPrim& b) { return a.z < b.z; });
        std::vector<float> rverts;
        std::vector<float> tverts;
        // rectBound / textBound 记录当前已经绑好的是哪个着色器与 VAO，
        // flush 之后置回 false，下次用到再重新绑，避免每批都重复绑
        bool rectBound = false, textBound = false;
        // 把攒下的矩形顶点一次性上传并绘制；顶点布局是 6 float/顶点（x,y,r,g,b,a）
        auto flushRect = [&]() {
            if (rverts.empty()) return;
            if (!rectBound) {
                rect2dShader_.Gvtcier_Shader_Use();
                rhi::Gvtcier_Rhi_BindVertexArray(rect2dVao_);
                rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, rect2dVbo_);
                rectBound = true;
            }
            rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(rverts.size() * sizeof(float)), rverts.data(), DynamicDraw);
            rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)(rverts.size() / 6));
            rverts.clear();
        };
        // 把一个字形追加成两个三角形（4 float/顶点：x,y,u,v）。
        // 屏幕像素坐标换算成 [-1,1] 的裁剪坐标，Y 轴要翻转（屏幕 Y 向下，NDC Y 向上）
        auto pushGlyph = [&](const FontGlyph* g, float penX, float baseY, float sc) {
            if (!g || g->w <= 0 || g->h <= 0) return;
            float x0 = penX + g->ox * sc;
            float y0 = baseY + (font_.ascent() - g->oy) * sc;
            float x1 = x0 + g->w * sc;
            float y1 = y0 + g->h * sc;
            float px0 = x0 / viewW_ * 2.0f - 1.0f;
            float py0 = 1.0f - y0 / viewH_ * 2.0f;
            float px1 = x1 / viewW_ * 2.0f - 1.0f;
            float py1 = 1.0f - y1 / viewH_ * 2.0f;
            float v[24] = {
                px0, py0, g->u0, g->v0,
                px1, py0, g->u1, g->v0,
                px0, py1, g->u0, g->v1,
                px1, py0, g->u1, g->v0,
                px1, py1, g->u1, g->v1,
                px0, py1, g->u0, g->v1,
            };
            for (int i = 0; i < 24; ++i) tverts.push_back(v[i]);
        };
        // 提交累积的文字顶点，用统一颜色并绑定字形所在图集页
        auto flushText = [&](float cr, float cg, float cb, float ca, int page) {
            if (tverts.empty()) return;
            if (!textBound) {
                rhi::Gvtcier_Rhi_BindVertexArray(textVao_);
                rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, textVbo_);
                textBound = true;
            }
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, font_.textureAt(page));
            textShader_.Gvtcier_Shader_Use();
            textShader_.Gvtcier_Shader_SetInt("uFont", 0);
            textShader_.Gvtcier_Shader_SetVec4("uColor", cr, cg, cb, ca);
            rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(tverts.size() * sizeof(float)), tverts.data(), DynamicDraw);
            rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)(tverts.size() / 4));
            tverts.clear();
        };
        for (const CanvasPrim& p : canvasItems_) {
            if (p.type == CanvasPrim::Rect) {
                flushText(1.0f, 1.0f, 1.0f, 1.0f, 0);
                textBound = false;
                float x0 = p.x, y0 = p.y, x1 = p.x + p.w, y1 = p.y + p.h;
                float px0 = x0 / viewW_ * 2.0f - 1.0f;
                float py0 = 1.0f - y0 / viewH_ * 2.0f;
                float px1 = x1 / viewW_ * 2.0f - 1.0f;
                float py1 = 1.0f - y1 / viewH_ * 2.0f;
                float v[36] = {
                    px0, py0, p.r, p.g, p.b, p.a,  px1, py0, p.r, p.g, p.b, p.a,
                    px1, py1, p.r, p.g, p.b, p.a,  px0, py0, p.r, p.g, p.b, p.a,
                    px1, py1, p.r, p.g, p.b, p.a,  px0, py1, p.r, p.g, p.b, p.a,
                };
                for (int i = 0; i < 36; ++i) rverts.push_back(v[i]);
            } else {
                flushRect();
                rectBound = false;
                // 字形按图集页分组：同一页的连续字形一起提交，页变了才切纹理
                const float px = p.scale * (32.0f / (float)FontAtlas::kBasePx);
                const char* s = p.text.c_str();
                int curPage = -1;
                float penX = p.x;
                while (*s) {
                    unsigned int cp = gvUtf8Next(s);
                    const FontGlyph* g = font_.glyph(cp);
                    if (!g) continue;
                    if (curPage >= 0 && g->page != curPage) {
                        flushText(p.r, p.g, p.b, p.a, curPage);
                    }
                    curPage = g->page;
                    pushGlyph(g, penX, p.y, px);
                    penX += g->advance * px;
                }
                if (curPage >= 0) flushText(p.r, p.g, p.b, p.a, curPage);
            }
        }
        flushRect();
        rhi::Gvtcier_Rhi_BindVertexArray(0);
    }
    canvasItems_.clear();
}

// 毛玻璃面板 pass，必须在主画面已经画完之后调用（需要读到完整背景来做模糊）。
//
// 步骤：
//   1. 按窗口尺寸建一条 4 级递减的模糊链（1/2、1/4、1/8、1/16 分辨率），
//      以及一张全分辨率背景拷贝 glassSceneTex_
//   2. 把默认帧缓冲的颜色读进 glassSceneTex_（ReadBufferTex 是一次带格式转换的拷贝）
//   3. 逐级降采样模糊，再逐级升采样叠回去（上下采样金字塔），得到近似大半径高斯模糊，
//      比直接做大核卷积快得多
//   4. 回到默认帧缓冲，按 z 排序逐块画面板，着色器用 uPanel/uRound 算圆角遮罩、
//      用 uTint 混一点灰、用 uAlpha 控制整体不透明度，背景同时采样原图与模糊图
// 结束后清空 glassPanels_ 并恢复主几何状态。
void Renderer::glassPass(const RenderOptions& opt) {
    if (glassPanels_.empty()) return;
    int w = opt.width, h = opt.height;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (glassSceneW_ != w || glassSceneH_ != h) {
        // 尺寸变化时重建整条模糊链，每级分辨率减半（>> (i+1)）
        for (int i = 0; i < 4; ++i)
            glassFbo_[i].createBlur(std::max(1, w >> (i + 1)), std::max(1, h >> (i + 1)));
        if (!glassSceneTex_) rhi::Gvtcier_Rhi_GenTextures(1, &glassSceneTex_);
        rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, glassSceneTex_);
        rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba8, w, h, Rgba, UnsignedByte, nullptr);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
        glassSceneW_ = w;
        glassSceneH_ = h;
    }
    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    rhi::Gvtcier_Rhi_ReadBuffer(ColorAttachment0);
    // 把默认帧缓冲（屏幕）上已画好的内容拷进 glassSceneTex_，作为玻璃的背景原图
    rhi::Gvtcier_Rhi_ReadBufferTex(Rgba, UnsignedByte, glassSceneTex_);
    glassFbo_[0].bind();
    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    blurShader_.Gvtcier_Shader_Use();
    blurShader_.Gvtcier_Shader_SetInt("uSrc", TexSlot::Src);
    blurShader_.Gvtcier_Shader_SetFloat("uIntensity", 1.0f);
    blurShader_.Gvtcier_Shader_SetVec2("uTexel", 1.0f / (float)w, 1.0f / (float)h);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Src, Tex2D, glassSceneTex_);
    fullscreenQuad_.draw();
    for (int i = 1; i < 4; ++i) {
        // 降采样：从上一级模糊到更小的下一级，uTexel 用源图尺寸
        glassFbo_[i].bind();
        blurShader_.Gvtcier_Shader_SetVec2("uTexel",
            1.0f / (float)glassFbo_[i - 1].width(), 1.0f / (float)glassFbo_[i - 1].height());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Src, Tex2D, glassFbo_[i - 1].colorTex());
        fullscreenQuad_.draw();
    }
    for (int i = 3; i >= 1; --i) {
        // 升采样：把最小的那级逐级放大叠回上一级，叠出来就是大半径模糊
        glassFbo_[i - 1].bind();
        blurShader_.Gvtcier_Shader_SetFloat("uIntensity", 1.0f);
        blurShader_.Gvtcier_Shader_SetVec2("uTexel",
            1.0f / (float)glassFbo_[i].width(), 1.0f / (float)glassFbo_[i].height());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Src, Tex2D, glassFbo_[i].colorTex());
        fullscreenQuad_.draw();
    }
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    rhi::Gvtcier_Rhi_Viewport(0, 0, w, h);
    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = true;
        st.blendSrc = SrcAlpha;
        st.blendDst = OneMinusSrcAlpha;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    std::stable_sort(glassPanels_.begin(), glassPanels_.end(),
                     [](const GlassPanel& a, const GlassPanel& b) { return a.z < b.z; });
    glassShader_.Gvtcier_Shader_Use();
    glassShader_.Gvtcier_Shader_SetInt("uScene", TexSlot::Scene);
    glassShader_.Gvtcier_Shader_SetInt("uBlur", TexSlot::Bloom);
    glassShader_.Gvtcier_Shader_SetVec2("uRes", (float)w, (float)h);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, glassSceneTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Bloom, Tex2D, glassFbo_[0].colorTex());
    rhi::Gvtcier_Rhi_BindVertexArray(rect2dVao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, rect2dVbo_);
    for (const GlassPanel& g : glassPanels_) {
        // 每个面板画一个全屏空间的四边形，圆角与着色都在片元着色器里按 uPanel 矩形判断。
        // 顶点 uv 在这里其实固定成 (0,1)/(1,0)，实际有意义的还是 uPanel 给出的屏幕矩形
        float x0 = g.x, y0 = g.y, x1 = g.x + g.w, y1 = g.y + g.h;
        float v[24] = {
            x0, y0, 0.0f, 1.0f,
            x1, y0, 1.0f, 1.0f,
            x0, y1, 0.0f, 0.0f,
            x1, y0, 1.0f, 1.0f,
            x1, y1, 1.0f, 0.0f,
            x0, y1, 0.0f, 0.0f,
        };
        glassShader_.Gvtcier_Shader_SetVec4("uPanel", x0, y0, g.w, g.h);
        glassShader_.Gvtcier_Shader_SetFloat("uAlpha", g.alpha);
        glassShader_.Gvtcier_Shader_SetFloat("uRound", g.radius);
        glassShader_.Gvtcier_Shader_SetFloat("uTint", g.tint);
        rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, sizeof(v), v, DynamicDraw);
        rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, 6);
    }
    rhi::Gvtcier_Rhi_BindVertexArray(0);
    glassPanels_.clear();
    {
        // 玻璃面板画完，恢复主几何状态供后续 2D 画布使用
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
}

// GTAO（地面真值环境光遮蔽）pass，输出一张 AO 图供延迟光照与 compose 使用。
//
// 分辨率：取主分辨率的 1/3（rw/3, rh/3）。AO 是低频信号，降分辨率算能省 9 倍开销，
// 最后双线性上采样即可，边缘靠去噪时的边缘感知权重保住。
//
// 步骤：
//   1. gtaoFbo_[0] 双附件（AO + 边缘信息）：用深度与法线做半球采样求遮蔽，
//      初始 AO 清成 1（完全不受遮蔽），所以被遮蔽处只会变暗不会变亮
//   2. 两趟去噪，都是边缘感知模糊，用第 0 张附件的边缘信息控制权重：
//      第一趟 beta=0.2 是固定的轻量模糊，先压掉采样噪声
//      第二趟用 opt.gtaoDenoiseBeta 由用户控制强度
//   3. 结果纹理存进 gtaoAOTex_，并记录尺寸；gtaoAOTex_ 为 0 表示本帧没有 AO
// 各采样参数（slices/steps/radius/intensity/power/bias/falloff）来自 RenderOptions，
// 由场景配置与画质设置决定。
void Renderer::gtaoPass(const Scene& scene, const Camera& cam, const RenderOptions& opt, int rw, int rh) {
    (void)scene;
    int gw = std::max(1, rw / 3), gh = std::max(1, rh / 3);
    if (gtaoFbo_[0].width() != gw || gtaoFbo_[0].height() != gh) {
        // 尺寸变化时重建三张 AO 缓冲：第 0 张双附件，第 1、2 张单附件做两级去噪
        gtaoFbo_[0].createR8(gw, gh, 2);
        gtaoFbo_[1].createR8(gw, gh, 1);
        gtaoFbo_[2].createR8(gw, gh, 1);
        gtaoLastW_ = 0.0f;
    }
    Mat4 proj = cam.projMatrix((float)opt.width / (float)opt.height);
    Mat4 invProj = proj.inverse();
    Mat4 view = cam.viewMatrix();

    gtaoFbo_[0].bind();
    // AO 初值清零会变成全黑；清成 1 表示“默认无遮蔽”，最终只有真正被挡住的像素才会小于 1
    rhi::Gvtcier_Rhi_ClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    rhi::Gvtcier_Rhi_Clear(ColorBit);
    {
        const unsigned bufs[2] = { ColorAttachment0, ColorAttachment1 };
        rhi::Gvtcier_Rhi_DrawBuffers(2, bufs);
        gtaoShader_.Gvtcier_Shader_Use();
        gtaoShader_.Gvtcier_Shader_SetInt("uDepth", TexSlot::Scene);
        gtaoShader_.Gvtcier_Shader_SetInt("uNormalTex", TexSlot::NormalTex);
        gtaoShader_.Gvtcier_Shader_SetInt("uSlices", opt.gtaoSlices);
        gtaoShader_.Gvtcier_Shader_SetInt("uSteps", opt.gtaoSteps);
        gtaoShader_.Gvtcier_Shader_SetFloat("uRadius", opt.gtaoRadius);
        gtaoShader_.Gvtcier_Shader_SetFloat("uIntensity", opt.gtaoIntensity);
        gtaoShader_.Gvtcier_Shader_SetFloat("uPower", opt.gtaoPower);
        gtaoShader_.Gvtcier_Shader_SetFloat("uBias", opt.gtaoBias);
        gtaoShader_.Gvtcier_Shader_SetFloat("uFalloffRange", opt.gtaoFalloff);
        gtaoShader_.Gvtcier_Shader_SetVec2("uRes", (float)gw, (float)gh);
        gtaoShader_.Gvtcier_Shader_SetMat4("uInvProj", invProj.m);
        gtaoShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
        gtaoShader_.Gvtcier_Shader_SetMat4("uView", view.m);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, hdrFbo_.depthTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::NormalTex, Tex2D, hdrFbo_.normalTex());
        fullscreenQuad_.draw();
    }

    // 边缘感知去噪：src 是 AO 输入，dst 是输出，beta 控制模糊强度（越大越模糊）。
    // 边缘信息固定取自 gtaoFbo_[0] 的第二附件，两趟共用同一份，保证不会把物体边界糊掉
    auto denoisePass = [&](ColorFbo& src, ColorFbo& dst, float beta) {
        dst.bind();
        gtaoBlurShader_.Gvtcier_Shader_Use();
        gtaoBlurShader_.Gvtcier_Shader_SetInt("uAO", TexSlot::AO);
        gtaoBlurShader_.Gvtcier_Shader_SetInt("uEdges", TexSlot::AOEdges);
        gtaoBlurShader_.Gvtcier_Shader_SetVec2("uRes", (float)gw, (float)gh);
        gtaoBlurShader_.Gvtcier_Shader_SetFloat("uBeta", beta);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AO, Tex2D, src.colorTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AOEdges, Tex2D, gtaoFbo_[0].normalTex());
        fullscreenQuad_.draw();
    };
    denoisePass(gtaoFbo_[0], gtaoFbo_[1], 0.2f);
    denoisePass(gtaoFbo_[1], gtaoFbo_[2], opt.gtaoDenoiseBeta);
    gtaoLastW_ = (float)rw;
    gtaoLastH_ = (float)rh;
    gtaoAOTex_ = gtaoFbo_[2].colorTex();
}

// =============================================================================
// 一帧的主渲染入口。整个管线按下面顺序执行，每个 pass 都在注释里标明输入输出：
//
//   1. 模型懒加载        把还没加载 mesh 的 Model 对象按名字加载出来
//   2. 特效同步          Effect 实例的播放/停止状态与场景对象对齐
//   3. BVH 重建          对象包围盒变了才重建，供视锥剔除使用
//   4. 收集光源          场景 Light 对象 + 粒子系统的瞬时光/常驻光
//   5. FBO 尺寸检查与清屏 尺寸变了重建 HDR/Bloom 链；清成天空色 + 深度清 0
//   6. 视锥剔除 + HZB 遮挡剔除  得到 visible 列表
//   7. 计算太阳屏幕位置   供天空着色器画太阳光斑
//   8. 天空 pass          画天空球，不写深度
//   9. 主几何 uniform 上传 + CPU tile 光照分配
//  10. 文本实体纹理生成   把 3D 世界里的文字用 GDI 烘成纹理
//  11. 卡通描边 pass      反向外壳法，只扩正面
//  12. 不透明几何         实例化/非实例化两套路径
//  13. 蒙皮网格
//  14. 特效粒子 + 特效实例
//  15. 调试线框
//  16. buildHzb           为下一帧的遮挡剔除准备深度
//  17. GTAO
//  18. 延迟光照           全屏 pass，读 G-Buffer 算出最终光照
//  19. 阴影调试探针
//  20. 透明几何           深度测试但不写深度，alpha 混合
//  21. 太阳阴影 pass      CSM 四层级联
//  22. 体积阴影 + 体积光
//  23. Bloom + 自动曝光 + compose + FXAA
//  24. glass pass         毛玻璃面板
//  25. FlushCanvas        2D 画布
//
// 全程使用反向 Z 的深度约定：深度缓冲清 0，深度测试用 Greater，
// 这样天空区（从未写入深度）的深度值恰好是 0，最远，任何几何都能覆盖上去。
//
// 上面这份 25 步清单挺漂亮，代价是这**一个函数 1300 多行**。
// 想改任何一步，都得先在这 1300 行里找到它，然后祈祷自己没踩到上下几步共享的
// 局部变量（rw/rh/visible/proj/view 这些都在函数开头定义，后面几十个 pass 全在用）。
// 这里还藏着一个经典顺序问题：第 21 步的太阳阴影 pass 排在
// 第 18 步延迟光照**后面** —— 也就是说这一帧的光照采样的是上一帧的阴影图。
// 平时看不出来，快速转视角时影子会慢半拍。改这里之前请先读三遍。
// =============================================================================
// 开场加载用：把一帧拆成几段，分几帧各跑一段。
// 下面这堆 pass 全靠同一个函数里的局部变量串起来，拆成独立函数要重写上千行，
// 所以做法还是整帧跑一遍，到传进来的这一段画完就退出，剩下的下一帧接着来。
void Renderer::Gvtcier_Render_RenderPhase(Scene& scene, const Camera& cam, const RenderOptions& optIn, int phase) {
    untilPhase_ = phase < 0 ? 0 : phase;
    Gvtcier_Render_Render(scene, cam, optIn);
}

void Renderer::Gvtcier_Render_Render(Scene& scene, const Camera& cam, const RenderOptions& optIn) {
    // 每一帧都从头开始，先把这一帧的终点放到底：正常渲染就是整帧画完，
    // 分段入口会事先把终点改小，这里不会动它
    phase_ = 0;
    RenderOptions opt = optIn;
    const Light& light = scene.light;
    int rw = 0, rh = 0;
    renderRes(rw, rh, opt);
    viewW_ = rw;
    viewH_ = rh;

    if (mode2D_) {
        // 纯 2D 模式：只画 2D 画布，直接返回，不碰任何 3D 资源
        viewW_ = opt.width;
        viewH_ = opt.height;
        rhi::Gvtcier_Rhi_Viewport(0, 0, opt.width, opt.height);
        renderCanvasItems2D();
        return;
    }

    static int dbgRw = -1, dbgRh = -1;
    // 分辨率变化时打一行日志，方便排查窗口缩放相关的问题
    if (rw != dbgRw || rh != dbgRh) {
        dbgRw = rw; dbgRh = rh;
        printf("[render] res %dx%d renderW_=%d renderH_=%d opt=%dx%d\n", rw, rh, renderW_, renderH_, opt.width, opt.height);
        fflush(stdout);
    }

    double tFrame0 = Gvtcier_NowSeconds();
    double tMark = tFrame0;
    // 各阶段累计耗时（秒）。用 tickMark 在阶段之间打点，累计值每 120 帧汇总输出一次性能日志
    double tShadow = 0.0, tGeom = 0.0, tGtao = 0.0, tVol = 0.0, tPost = 0.0, tCanvas = 0.0;
    auto tickMark = [&](double& out) { double n = Gvtcier_NowSeconds(); out += n - tMark; tMark = n; };

    // ---------- pass 1：模型懒加载 ----------
    // 场景里按名字引用的模型可能还没真正加载 mesh，这里补上。
    // 放在渲染帧开头做，保证首次出现的模型当帧就能显示。
    scene.ForEachObject([&](Object& to, GvtId<Object>) {
        if (to.kind != ObjKind::Model) return;
        if (to.mesh.valid()) return;
        if (to.modelName.empty()) return;
        to.mesh = scene.Gvtcier_Scene_LoadModelObject(to.modelName.c_str());
    });

    // ---------- pass 2：特效同步 ----------
    // 把场景里 Effect 对象的播放状态与特效系统对齐（新增的播放、删掉的停止），
    // 再推进粒子系统一帧。固定按 1/60 秒步进，与真实帧率解耦，保证粒子动画速度稳定。
    Gvtcier_Render_SyncSceneEffects(scene);
    effectSystem_.Gvtcier_Effect_Update(1.0f / 60.0f, fx_);
    // effectType == 1 的 Effect 是激光束：每次给它加两条不同宽度的光束，
    // 窄的当核心、宽的当辉光（2.4 倍宽），这是常见的低成本激光画法。
    // 颜色按 emissive 放大 0.7~3.2 倍，让高自发光的光束在 HDR 下能被 Bloom 抓到
    scene.ForEachObject([&](const Object& eo, GvtId<Object>) {
        if (eo.kind != ObjKind::Effect) return;
        if (!eo.visible) return;
        if (eo.effectType != 1) return;
        FxColor ec = { eo.lightColor.x * (0.7f + eo.effectEmissive * 2.5f),
                       eo.lightColor.y * (0.7f + eo.effectEmissive * 2.5f),
                       eo.lightColor.z * (0.7f + eo.effectEmissive * 2.5f), 1.0f };
        Vec3 a = eo.pos + eo.laserA;
        Vec3 b = eo.pos + eo.laserB;
        float w = eo.laserWidth * (0.6f + eo.effectParam0 * 0.8f);
        fx_.Gvtcier_Fx_AddBeam(a, b, ec, w);
        fx_.Gvtcier_Fx_AddBeam(a, b, ec, w * 2.4f);
    });

    // 统一改回填充模式：上一个 pass 可能切过线框
    g_rs.polygonMode = rhi::Fill;
    rsFlush();

    // ---------- pass 3：BVH 重建 ----------
    // 只有对象的变换或包围盒脏了、或者 BVH 根本还没建，才重建。
    // 重建时按“可见且有 mesh”重新收集包围盒，并同步维护 BVH 索引 -> 对象 ID 的映射表
    // objIndexBuf_，后面剔除结果就是这张表的下标。
    {
        std::vector<AABB3>& objBounds = objBoundsBuf_;
        std::vector<GvtId<Object>>& objIndexMap = objIndexBuf_;
        bool anyCacheDirty = false;
        scene.ForEachObject([&](const Object& o, GvtId<Object>) {
            if (anyCacheDirty) return;
            if (!o.visible || !scene.Gvtcier_Scene_GetMesh(o.mesh)) return;
            if (o.matrixDirty || o.boundsDirty) anyCacheDirty = true;
        });
        if (anyCacheDirty || !bvh_.built()) {
            objBounds.clear();
            objIndexMap.clear();
            objBounds.reserve(scene.Gvtcier_Scene_ObjectCount());
            objIndexMap.reserve(scene.Gvtcier_Scene_ObjectCount());
            scene.ForEachObject([&](const Object& o, GvtId<Object> id) {
                if (!o.visible) return;
                if (!scene.Gvtcier_Scene_GetMesh(o.mesh)) return;
                objBounds.push_back(o.Gvtcier_Object_Bounds(scene.Gvtcier_Scene_MeshHalfExtents(o.mesh)));
                objIndexMap.push_back(id);
            });
            bvh_.build(objBounds);
        }
    }

    // ---------- pass 4：收集光源 ----------
    // 三路来源：场景里的点光/聚光/平行光对象、粒子系统的瞬时动态光、常驻光。
    // 点光与聚光都塞进 fxLights 参与 tile 光照分配，平行光单独用数组 uniform 上传。
    // 方向统一取对象本地 -Z 轴（srz * -1），与相机朝向约定一致。
    std::vector<FxLight> fxLights;
    std::vector<Vec4> dirls;
    std::vector<Vec4> dircols;
    scene.ForEachObject([&](const Object& ol, GvtId<Object>) {
        if (ol.kind == ObjKind::LightPoint) {
            fxLights.push_back({ ol.pos, FxColor{ ol.lightColor.x, ol.lightColor.y, ol.lightColor.z, 1.0f }, ol.lightRange, ol.lightShadow, ol.lightIntensity });
        } else if (ol.kind == ObjKind::LightSpot) {
            Mat4 srm = Quat::fromEulerDeg(ol.rotDeg.x, ol.rotDeg.y, ol.rotDeg.z).toMat4();
            Vec3 srz(srm.m[8], srm.m[9], srm.m[10]);
            Vec3 sdir = (srz * -1.0f).normalized();
            float scos = std::cos(ol.spotAngle * 3.14159265f / 180.0f);
            fxLights.push_back({ ol.pos, FxColor{ ol.lightColor.x, ol.lightColor.y, ol.lightColor.z, 1.0f }, ol.lightRange, ol.lightShadow, ol.lightIntensity, sdir, scos });
        } else if (ol.kind == ObjKind::LightDir) {
            Mat4 rm = Quat::fromEulerDeg(ol.rotDeg.x, ol.rotDeg.y, ol.rotDeg.z).toMat4();
            Vec3 rz(rm.m[8], rm.m[9], rm.m[10]);
            Vec3 d = (rz * -1.0f).normalized();
            if ((int)dirls.size() < Gvtcier_Limits().maxDirLights) {
                dirls.push_back(Vec4(d.x, d.y, d.z, 1.0f));
                dircols.push_back(Vec4(ol.lightColor.x, ol.lightColor.y, ol.lightColor.z, 1.0f));
            }
        }
    });
    if (flashlightOn_) {
        Vec3 fdir = cam.forward();
        Vec3 fpos = cam.pos + fdir * 0.3f;
        fxLights.push_back({ fpos, FxColor{ 1.0f, 0.97f, 0.9f, 1.0f }, 26.0f, true, 5.0f,
                             fdir, std::cos(19.0f * 3.14159265f / 180.0f) });
    }
    const std::vector<FxLight>& fxTransient = fx_.lights();
    // 预留容量，避免下面两次 insert 触发扩容拷贝
    fxLights.reserve(fxLights.size() + fxTransient.size() + fx_.persistentLights().size() + 8);
    fxLights.insert(fxLights.end(), fxTransient.begin(), fxTransient.end());
    const std::vector<FxLight>& fxPersist = fx_.persistentLights();
    fxLights.insert(fxLights.end(), fxPersist.begin(), fxPersist.end());
    pointShadowCount_ = 0;
    if (pointDepthShader_.Gvtcier_Shader_Id() != 0) {
        for (int i = 0; i < (int)fxLights.size() && pointShadowCount_ < kPointShadowLights; ++i) {
            const FxLight& fl = fxLights[i];
            if (!fl.castShadow || fl.radius <= 0.0f) continue;
            pointShadowIdx_[pointShadowCount_] = i;
            pointShadowPos_[pointShadowCount_] = fl.pos;
            pointShadowFar_[pointShadowCount_] = fl.radius + 1.0f;
            ++pointShadowCount_;
        }
    }

    tickMark(tShadow);
    // ---------- pass 5：FBO 尺寸检查与清屏 ----------
    // 分辨率变了就重建整个 HDR 管线：四附件 HDR 缓冲、LDR 缓冲（FXAA 中转）、
    // 以及 4 级 Bloom 金字塔（每级分辨率减半，>> (i+1)）
    if (hdrFbo_.width() != rw || hdrFbo_.height() != rh) {
        hdrFbo_.create(rw, rh, true);
        ldrFbo_.create(rw, rh, false);
        for (int i = 0; i < 4; ++i)
            bloomMips_[i].create(std::max(1, rw >> (i + 1)), std::max(1, rh >> (i + 1)), false);
    }
    // FXAA 需要逐像素精确采样，所以开 FXAA 时颜色图用最近点过滤，关掉时才允许线性放大
    hdrFbo_.setColorFilter(fxaaOn_);
    hdrFbo_.bind();
    {
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = opt.wireframe ? Line : Fill;
        applyPassState(st);
    }
    // 整个 HDR 缓冲清成天空色：天空 pass 虽然会覆盖全屏，但万一天空球有缝隙，
    // 背景也是合理的天空色而不是黑边。深度同时清 0（反向 Z 下的最远值）
    rhi::Gvtcier_Rhi_ClearColor(scene.skyColor.x, scene.skyColor.y, scene.skyColor.z, 1.0f);
    rhi::Gvtcier_Rhi_Clear(ColorBit | DepthBit);
    // 逐阶段构建的时候，天空这一步跟下面那步是分开调用的，这里先把这一帧的进度记下来
    phase_ = 1;
    if (Gvtcier_StageOver()) return;

    Mat4 proj = cam.projMatrix((float)opt.width / (float)opt.height);
    Mat4 view = cam.viewMatrix();

    Mat4 cullMvp = proj * view;
    const double* cm = cullMvp.m;
    // 视锥 6 个平面用 Gribb-Hartmann 法从视图投影矩阵提取：
    // 每行取 x/y/z 行与 w 行的加减组合，再归一化。注意这里的 +y/-y 顺序与 CSM 里的略有不同，
    // 不影响判断结果（平面只是上下互换了一个符号）
    auto plane = [](double a, double b, double c, double d) {
        double len = std::sqrt(a * a + b * b + c * c);
        if (len < 1e-20) len = 1.0;
        Plane3 p = { Vec3(-a / len, -b / len, -c / len), d / len };
        return p;
    };
    Plane3 planes[6] = {
        plane(cm[3] + cm[2], cm[7] + cm[6], cm[11] + cm[10], cm[15] + cm[14]),
        plane(cm[3] - cm[2], cm[7] - cm[6], cm[11] - cm[10], cm[15] - cm[14]),
        plane(cm[3] + cm[0], cm[7] + cm[4], cm[11] + cm[8], cm[15] + cm[12]),
        plane(cm[3] - cm[1], cm[7] - cm[5], cm[11] - cm[9], cm[15] - cm[13]),
        plane(cm[3] - cm[0], cm[7] - cm[4], cm[11] - cm[8], cm[15] - cm[12]),
        plane(cm[3] + cm[1], cm[7] + cm[5], cm[11] + cm[9], cm[15] + cm[13]),
    };
    // ---------- pass 6：视锥剔除 + HZB 遮挡剔除 ----------
    // 第一步用 BVH 做视锥剔除，得到候选 list；第二步对候选逐个做遮挡判断。
    // 遮挡剔除故意做得保守，以下情况一律保留，宁可多画：
    //   - 包围盒有角点跑到相机后面（穿越投影无法准确判断屏幕范围）
    //   - 包围盒部分或几乎全部在屏幕外
    //   - 离相机太近（3 米内，屏幕上很大，遮挡判断容易出错）
    //   - 尺寸太大（最大边长超过 12，通常是地形/大建筑，本来就不该被剔除）
    std::vector<int>& visible = visibleList_;
    visible.clear();
    bvh_.queryFrustum(planes, 6, visible);
    if (!visible.empty()) {
        std::vector<int> kept;
        kept.reserve(visible.size());
        // 3 米是“离相机太近不参与遮挡剔除”的距离阈值
        const float kOcclusionMinDist = 3.0f;
        for (size_t vi = 0; vi < visible.size(); ++vi) {
            int bi = visible[vi];
            const AABB3& bb = objBoundsBuf_[bi];
            Vec3 mn = aabbMin(bb);
            Vec3 mx = aabbMax(bb);
            float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
            bool anyFront = false;
            bool allFront = true;
            for (int c = 0; c < 8; ++c) {
                Vec3 p((c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z);
                Vec4 clip = proj * view * Vec4(p.x, p.y, p.z, 1.0f);
                if (clip.w <= 1e-5f) { allFront = false; continue; }
                anyFront = true;
                float nx = clip.x / clip.w;
                float ny = clip.y / clip.w;
                if (nx < bx0) bx0 = nx;
                if (ny < by0) by0 = ny;
                if (nx > bx1) bx1 = nx;
                if (ny > by1) by1 = ny;
            }
            if (!anyFront) continue;
            // 有角点在相机后面：屏幕范围算不准，直接保留
            if (!allFront) { kept.push_back(bi); continue; }
            // 屏幕矩形整体在视野外（放宽 0.02 的余量避免边界抖动），剔除
            if (bx1 < -1.02f || by1 < -1.02f || bx0 > 1.02f || by0 > 1.02f) continue;
            if (occlusionOn_ && hzbValid_) {
                // 取包围盒上离相机最近的点，算到相机的距离，判断是否属于“近处不剔”
                Vec3 nearPoint = aabbClosestPoint(bb, cam.pos);
                Vec3 toCam = nearPoint - cam.pos;
                float dist = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
                bool nearOrHuge = dist < kOcclusionMinDist;
                // 12 米是“物体最大边长超过就不参与遮挡剔除”的阈值
                float ext = std::fmax(std::fmax(bb.half.x, bb.half.y), bb.half.z) * 2.0f;
                if (!nearOrHuge && ext <= 12.0f && hzbTest(bb, hzbView_, hzbProj_)) {
                    ++occludedCount_;
                    continue;
                }
            }
            kept.push_back(bi);
        }
        // 用保留列表替换原候选列表
        visible.swap(kept);
    }

    // ---------- pass 7：计算太阳屏幕位置 ----------
    // scene.light.dir 是光线方向，太阳在反方向；归一化后把太阳放到相机前方 1500 单位处，
    // 再投影到 NDC，得到太阳在屏幕上的位置，交给天空着色器画光斑与光晕。
    Vec3 sunDirW(-light.dir.x, -light.dir.y, -light.dir.z);
    float sdl = std::sqrt(sunDirW.x * sunDirW.x + sunDirW.y * sunDirW.y + sunDirW.z * sunDirW.z);
    if (sdl > 1e-6f) { sunDirW.x /= sdl; sunDirW.y /= sdl; sunDirW.z /= sdl; }

    // 1500 是太阳这个“虚拟点”到相机的距离，只要大于天空球半径（900）且小于远平面（2000）即可，
    // 取中间值能让太阳的位置在投影上稳定、不会因深度截断缺失
    Vec3 sunPos = cam.pos + sunDirW * 1500.0f;

    // 把太阳投到 NDC。w<0 表示太阳在相机背后，此时 NDC 坐标会翻过来，
    // 需要把方向重新推到屏幕外（保持光晕方向正确但不出现太阳本体）
    Vec4 sunClip = proj * view * Vec4(sunPos.x, sunPos.y, sunPos.z, 1.0f);
    Vec2 sunScreenPos(0.0f, 0.0f);
    if (sunClip.w != 0.0f) {
        sunScreenPos.x = sunClip.x / sunClip.w;
        sunScreenPos.y = sunClip.y / sunClip.w;
    }
    if (sunClip.w < 0.0f) {
        // 用最大分量的绝对值当比例缩放，把位置推到 |x| 或 |y| = 2 的屏幕外；
        // 太阳几乎正对背后时方向不稳定，直接放到屏幕下方的固定位置
        float s = std::fmin(1.0f, std::fmax(std::fabs(sunScreenPos.x), std::fabs(sunScreenPos.y)));
        if (s < 0.05f) { sunScreenPos.x = 0.0f; sunScreenPos.y = -2.0f; }
        else { sunScreenPos.x = sunScreenPos.x / s * 2.0f; sunScreenPos.y = sunScreenPos.y / s * 2.0f; }
    }

    // ---------- pass 8：天空 pass ----------
    // 天空球关深度测试与写入、不剔面、不混合，直接覆盖整屏。
    // 这样它天然排在所有几何之前/之后都不影响结果，也不会污染深度缓冲
    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.depthFunc = Greater;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    if (scene.showSkybox) {
    skyShader_.Gvtcier_Shader_Use();
    skyShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
    skyShader_.Gvtcier_Shader_SetMat4("uView", view.m);
    {
        Mat4 skyVP = proj * view;
        skyShader_.Gvtcier_Shader_SetMat4("uInvVP", skyVP.inverse().m);
    }
    skyShader_.Gvtcier_Shader_SetVec3("uCamPos", cam.pos.x, cam.pos.y, cam.pos.z);
    skyShader_.Gvtcier_Shader_SetVec3("uSunPos", sunPos.x, sunPos.y, sunPos.z);
    skyShader_.Gvtcier_Shader_SetVec2("uSunScreenPos", sunScreenPos.x, sunScreenPos.y);
    skyShader_.Gvtcier_Shader_SetVec3("uSunDir", sunDirW.x, sunDirW.y, sunDirW.z);
    skyShader_.Gvtcier_Shader_SetFloat("uTime", (float)engineTime());
    // 太阳颜色乘 1.3：天空里的太阳光斑要比实际光照色更亮一点，才能在 HDR 下被 Bloom 抓到
    skyShader_.Gvtcier_Shader_SetVec3("uSunColor", light.color.x * 1.3f, light.color.y * 1.3f, light.color.z * 1.3f);
    // 太阳“露脸”程度：太阳在屏幕内时给 1，越靠边越淡到 0。
    // 0.85 是开始淡出的位置（NDC 0.85 之外），0.3 是淡出区间宽度，smoothstep 做平滑过渡
    float sunOn = 0.0f;
    if (sunClip.w > 0.0f) {
        float nx = sunClip.x / sunClip.w;
        float ny = sunClip.y / sunClip.w;
        float d = std::fmax(std::fabs(nx), std::fabs(ny));
        float t = (d - 0.85f) / 0.3f;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        t = t * t * (3.0f - 2.0f * t);
        sunOn = 1.0f - t;
    }
    if (sunOn > 0.0f && scene.sunEnabled) {
        // 0.12 是平滑系数：每帧向目标值靠拢 12%，大约 8 帧到位。
        // 这样相机快速转动时太阳的亮度变化不会突然跳变
        sunOnSmooth_ += (1.0f - sunOnSmooth_) * 0.12f;
    } else {
        sunOnSmooth_ += (0.0f - sunOnSmooth_) * 0.12f;
    }
    skyShader_.Gvtcier_Shader_SetFloat("uSunOnScreen", scene.sunEnabled ? sunOnSmooth_ : 0.0f);
    skyShader_.Gvtcier_Shader_SetFloat("uAspect", (float)opt.width / (float)opt.height);
    buildSkyLut(scene.skyColor);
    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::SkyGradient, Tex2D, skyGradLut_);
    skyShader_.Gvtcier_Shader_SetInt("uGradLut", TexSlot::SkyGradient);
    skySphere_.draw();
    }
    {
        // 天空画完，恢复主几何状态（开深度、剔背面）
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = opt.wireframe ? Line : Fill;
        applyPassState(st);
    }
    // 天空结束，这一帧的进度推进到几何
    phase_ = 2;
    // 分段渲染时到这就收工，把几何留给下一帧
    if (Gvtcier_StageOver()) return;

    // ---------- pass 9：主几何 uniform 上传 + CPU tile 光照分配 ----------
    // 这里上传与对象无关的全局 uniform（相机、投影、主光、雾、材质开关），
    // 逐对象的部分在下面的绘制循环里上传。
    mainShader_.Gvtcier_Shader_Use();
    mainShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
    mainShader_.Gvtcier_Shader_SetMat4("uView", view.m);
    mainShader_.Gvtcier_Shader_SetVec3("uCamPos", cam.pos.x, cam.pos.y, cam.pos.z);
    mainShader_.Gvtcier_Shader_SetVec3("uLightDir", light.dir.x, light.dir.y, light.dir.z);
    // 太阳关闭时把主光颜色置零，等价于关掉直射光只留环境光
    Vec3 mainCol = scene.sunEnabled ? light.color : Vec3(0, 0, 0);
    mainShader_.Gvtcier_Shader_SetVec3("uLightColor", mainCol.x, mainCol.y, mainCol.z);
    mainShader_.Gvtcier_Shader_SetInt("uDirLightCount", (int)dirls.size());
    {
        // 只有太阳开启、需要阴影、且阴影图确实有 4 层时才启用 CSM，否则着色器走无阴影分支
        bool csmOn = (scene.sunEnabled && opt.showShadow && sunShadow_.layers() >= 4);
        uploadCsmUniforms(mainShader_, csmOn);
        uploadAsmUniforms(mainShader_, csmOn);
        if (csmOn) {
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::CSMShadow, Tex2DArray, sunShadow_.depthTex());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AsmShadow, Tex2DArray, asmShadow_.depthTex());
        }
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube, TexCube, pointShadow_[0].cubeTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube2, TexCube, pointShadow_[1].cubeTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube3, TexCube, pointShadow_[2].cubeTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube4, TexCube, pointShadow_[3].cubeTex());
    }
    uploadDirLightUniforms(mainShader_, dirls, dircols);
    mainShader_.Gvtcier_Shader_SetInt("uDirShadowCount", 0);
    mainShader_.Gvtcier_Shader_SetFloat("uUvScale", 1.0f);
    mainShader_.Gvtcier_Shader_SetFloat("uTime", (float)engineTime());
    // 环境光总强度：关闭环境光时传 0，相当于不着色
    mainShader_.Gvtcier_Shader_SetFloat("uAmbientMul", scene.ambientEnabled ? scene.ambientIntensity : 0.0f);
    mainShader_.Gvtcier_Shader_SetVec3("uAmbientColor", scene.ambientColor.x, scene.ambientColor.y, scene.ambientColor.z);
    mainShader_.Gvtcier_Shader_SetInt("uAlbedo", TexSlot::Albedo);
    mainShader_.Gvtcier_Shader_SetInt("uFaceTex", TexSlot::FaceTex);
    {
        static int dbgFrame = 0;
        if (++dbgFrame % 120 == 1)
            DBGLN("[render] f%d sun=%d showShadow=%d sunColor=%.2f,%.2f,%.2f camPos=%.1f,%.1f,%.1f",
                  dbgFrame, (int)scene.sunEnabled, (int)opt.showShadow,
                  scene.light.color.x, scene.light.color.y, scene.light.color.z,
                  cam.pos.x, cam.pos.y, cam.pos.z);
    }
    mainShader_.Gvtcier_Shader_SetVec3("uFogColor", scene.skyColor.x, scene.skyColor.y, scene.skyColor.z);
    // 只有 fogLevel_ == 1（距离雾）时主几何着色器才算距离雾；2/3 档的雾交给体积光 pass 处理，
    // 所以这里把密度乘 0 关掉，避免两层雾叠在一起变浑
    float fogMul = (fogLevel_ == 1) ? 1.0f : 0.0f;
    mainShader_.Gvtcier_Shader_SetFloat("uFogDensity", scene.fogDensity * fogMul);
    mainShader_.Gvtcier_Shader_SetInt("uSpecular", specularOn_ ? 1 : 0);
    // 64 是高光指数（Blinn-Phong 的 shininess）：越大高光越小越锐利。
    // 64 大致对应“抛光金属/湿表面”的手感，是卡通渲染里常用的硬高光取值
    mainShader_.Gvtcier_Shader_SetFloat("uShininess", 64.0f);
    mainShader_.Gvtcier_Shader_SetInt("uLightLimit", lightLimit_);

    int lightCount = (int)fxLights.size();
    // 屏幕按 tileSize×tileSize 切块（引擎默认 16，即 16x16 像素一块），
    // tileW_/tileH_ 是横竖方向的块数，向上取整保证边缘不留空
    const int kTileSize = Gvtcier_Limits().tileSize;
    const int kMaxPerTile = Gvtcier_Limits().maxLightsPerTile;
    tileW_ = (rw + kTileSize - 1) / kTileSize;
    tileH_ = (rh + kTileSize - 1) / kTileSize;
    size_t tileTotal = (size_t)tileW_ * tileH_;

    // 光源数据打包成扁平 float 数组：第 0 个元素是光源数量，之后每个光源 13 个 float
    // （位置 xyz、半径、颜色 rgb、强度、方向 xyz、聚光夹角余弦、阴影槽位 -1 表示无阴影）
    lightDataBuf_.resize(1 + (size_t)lightCount * 13);
    lightDataBuf_[0] = (float)lightCount;
    for (int i = 0; i < lightCount; ++i) {
        const FxLight& fl = fxLights[i];
        float* ld = &lightDataBuf_[1 + (size_t)i * 13];
        ld[0] = fl.pos.x;
        ld[1] = fl.pos.y;
        ld[2] = fl.pos.z;
        ld[3] = fl.radius;
        ld[4] = fl.color.r;
        ld[5] = fl.color.g;
        ld[6] = fl.color.b;
        ld[7] = fl.intensity;
        ld[8] = fl.dir.x;
        ld[9] = fl.dir.y;
        ld[10] = fl.dir.z;
        ld[11] = fl.spotCos;
        ld[12] = -1.0f;
    }
    for (int pi = 0; pi < pointShadowCount_; ++pi) {
        float* ld = &lightDataBuf_[1 + (size_t)pointShadowIdx_[pi] * 13];
        ld[12] = (float)pi;
    }

    rhi::Gvtcier_Rhi_BindBuffer(ShaderStorageBuffer, lightSsbo_);
    rhi::Gvtcier_Rhi_BufferData(ShaderStorageBuffer, (size_t)(lightDataBuf_.size() * sizeof(float)), lightDataBuf_.data(), DynamicDraw);

    if (lightCount == 0) {
        // 没有动态光源时也要把三个 SSBO 绑好并填上最小数据：
        // 着色器里 tile 表是必读的，缓冲为空会读到垃圾甚至越界，所以塞两个 0 保证 tile 数为 0。
        static const unsigned int kZeroTile[2] = { 0, 0 };
        rhi::Gvtcier_Rhi_BindBuffer(ShaderStorageBuffer, tileSsbo_);
        rhi::Gvtcier_Rhi_BufferData(ShaderStorageBuffer, sizeof(kZeroTile), kZeroTile, DynamicDraw);
        rhi::Gvtcier_Rhi_BindBuffer(ShaderStorageBuffer, idxSsbo_);
        rhi::Gvtcier_Rhi_BufferData(ShaderStorageBuffer, sizeof(kZeroTile), nullptr, DynamicDraw);
        // 0/1/2 号绑定点是着色器里约定的 light / tile / index 三张表
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 0, lightSsbo_);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 1, tileSsbo_);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 2, idxSsbo_);
    } else {
        // ---------------------------------------------------------------
        // CPU 端 tile 光照分配（三遍扫描）
        // 屏幕按 kTileSize×kTileSize 切成格子（引擎默认 16×16），每个格子记一个
        // “光照索引段”——段里是这个格子需要计算的所有光源下标。
        // 第一遍：逐光源求出它覆盖的 tile 矩形范围，并给范围内每个 tile 计数 +1
        // 第二遍：对计数做前缀和，得到每个 tile 在索引数组里的起始偏移
        // 第三遍：再走一遍光源，用游标把光源下标回填到各自 tile 的段里
        // ---------------------------------------------------------------
        tileCountBuf_.assign(tileTotal, 0);
        lightTileRanges_.resize(lightCount);
        // proj.m[5] 是投影矩阵的 y 缩放 = 1/tan(fovY/2)，所以 tan 取倒数；
        // projScale 把“视空间距离”换算成“屏幕像素”，用于求光源的屏幕半径。
        float tanHalfFov = 1.0f / proj.m[5];
        float projScale = (float)rh * 0.5f / tanHalfFov;
        float screenW = (float)rw;
        float screenH = (float)rh;
        for (int i = 0; i < lightCount; ++i) {
            const FxLight& fl = fxLights[i];
            TileRange& r = lightTileRanges_[i];
            Vec3 viewPos = view * fl.pos;
            // 在视空间里 z 是负的（相机朝 -Z 看），z > radius 说明光源整个跑到相机背后了
            if (viewPos.z > fl.radius) { r.all = false; r.x1 = -1; continue; }
            // 光源中心已经越过近平面附近：直接判成“覆盖全屏”，
            // 这样近处的大范围光不会因为投影除零算出一堆错误的 tile
            if (viewPos.z > -0.1f) {
                if (viewPos.length() > fl.radius) { r.all = false; r.x1 = -1; continue; }
                r.all = true;
                r.x0 = 0; r.y0 = 0;
                r.x1 = tileW_ - 1; r.y1 = tileH_ - 1;
                for (size_t t = 0; t < tileTotal; ++t) ++tileCountBuf_[t];
                continue;
            }
            // 屏幕半径用最近的深度算（depth - radius），保守取最大值，
            // 保证光源在该范围内任何一点都能照亮格子而不会被裁掉
            float depth = -viewPos.z;
            float nearDepth = depth - fl.radius;
            if (nearDepth < 0.01f) nearDepth = 0.01f;
            float scrRadius = fl.radius * projScale / nearDepth;
            // 视空间坐标乘 projScale 变成像素偏移，再加半屏得到屏幕中心坐标
            float sx = (viewPos.x / depth) * projScale + screenW * 0.5f;
            float sy = (viewPos.y / depth) * projScale + screenH * 0.5f;
            // 四周各外扩 1 个 tile 作为安全边距，抵消浮点误差与近似半径带来的漏光
            int x0 = (int)((sx - scrRadius) / kTileSize) - 1;
            int x1 = (int)((sx + scrRadius) / kTileSize) + 1;
            int y0 = (int)((sy - scrRadius) / kTileSize) - 1;
            int y1 = (int)((sy + scrRadius) / kTileSize) + 1;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= tileW_) x1 = tileW_ - 1;
            if (y1 >= tileH_) y1 = tileH_ - 1;
            r.all = false;
            r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
            if (x1 < x0 || y1 < y0) { r.x1 = -1; continue; }
            for (int ty = y0; ty <= y1; ++ty) {
                unsigned int* row = &tileCountBuf_[(size_t)ty * tileW_];
                for (int tx = x0; tx <= x1; ++tx) ++row[tx];
            }
        }

        // 每个 tile 在 tileDataBuf_ 里占两个 uint：[0] 光源个数，[1] 索引起始偏移。
        // 这里一边累加 offset 一边写前缀和，offset 最终就是索引数组的总长度。
        tileDataBuf_.resize(tileTotal * 2);
        unsigned int offset = 0;
        for (size_t t = 0; t < tileTotal; ++t) {
            tileDataBuf_[t * 2 + 0] = tileCountBuf_[t];
            tileDataBuf_[t * 2 + 1] = offset;
            offset += tileCountBuf_[t];
        }
        lightIdxBuf_.resize(offset);
        // tileCursorBuf_ 初始化为每个 tile 的起始偏移，回填时逐格自增即可
        tileCursorBuf_.resize(tileTotal);
        for (size_t t = 0; t < tileTotal; ++t) tileCursorBuf_[t] = tileDataBuf_[t * 2 + 1];

        for (int i = 0; i < lightCount; ++i) {
            const TileRange& r = lightTileRanges_[i];
            if (r.x1 < r.x0 && !r.all) continue;
            if (r.all) {
                for (size_t t = 0; t < tileTotal; ++t) lightIdxBuf_[tileCursorBuf_[t]++] = (unsigned int)i;
                continue;
            }
            for (int ty = r.y0; ty <= r.y1; ++ty) {
                unsigned int* row = &tileCursorBuf_[(size_t)ty * tileW_];
                for (int tx = r.x0; tx <= r.x1; ++tx)
                    lightIdxBuf_[row[tx]++] = (unsigned int)i;
            }
        }

        rhi::Gvtcier_Rhi_BindBuffer(ShaderStorageBuffer, tileSsbo_);
        rhi::Gvtcier_Rhi_BufferData(ShaderStorageBuffer, (size_t)(tileDataBuf_.size() * sizeof(unsigned int)), tileDataBuf_.data(), DynamicDraw);
        rhi::Gvtcier_Rhi_BindBuffer(ShaderStorageBuffer, idxSsbo_);
        rhi::Gvtcier_Rhi_BufferData(ShaderStorageBuffer, (size_t)(lightIdxBuf_.size() * sizeof(unsigned int)), lightIdxBuf_.empty() ? nullptr : lightIdxBuf_.data(), DynamicDraw);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 0, lightSsbo_);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 1, tileSsbo_);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 2, idxSsbo_);
    }

    mainShader_.Gvtcier_Shader_Use();
    // tile 宽度与块尺寸也传给着色器，着色器用屏幕像素坐标除以块尺寸就能定位自己的 tile
    mainShader_.Gvtcier_Shader_SetInt("uTileW", tileW_);
    mainShader_.Gvtcier_Shader_SetInt("uTileSize", kTileSize);

    // ---------- pass 10：文本实体纹理生成 ----------
    // 场景里的 Text 对象不能直接用字体图集（那个是屏幕空间 2D 用的），
    // 所以这里用 Windows GDI 把文本（微软雅黑加粗 64 号）画进位图，再转成纹理供 3D 贴面。
    // 缓存键是“文本内容 + 颜色”，内容或颜色变了才重烘，最多存 64 条，
    // 超了就从最老的一半开始逐条注销纹理并丢弃。
    // 颜色通过 GDI 的白色字形 + 后面统一染色的方式实现：读出的字是白色，
    // 转成 RGBA 时把 RGB 全部替换成目标颜色，Alpha 取字形的灰度（三通道最大值）。
    scene.ForEachObject([&](Object& to, GvtId<Object>) {
        if (to.kind != ObjKind::Text) return;
        if (!to.mesh.valid()) to.mesh = scene.planeMesh();
        if (to.text.empty()) { to.tex = scene.whiteTex(); return; }
        if (textEntityTexs_.size() >= 64) {
            auto oldest = textEntityTexs_.begin();
            std::advance(oldest, textEntityTexs_.size() / 2);
            for (auto itr = textEntityTexs_.begin(); itr != oldest; ++itr)
                scene.Gvtcier_Scene_UnregisterTexture(itr->second.tex);
            textEntityTexs_.erase(textEntityTexs_.begin(), oldest);
        }
        char ck[160];
        // 缓存键 = 文本 + 三位小数的颜色；颜色改了就要重烘，因为颜色是烘进像素里的
        snprintf(ck, sizeof(ck), "%s|%.2f,%.2f,%.2f", to.text.c_str(),
                 to.lightColor.x, to.lightColor.y, to.lightColor.z);
        auto it = textEntityTexs_.find(ck);
        if (it == textEntityTexs_.end()) {
#ifdef _WIN32
            // 4095 是单次 GDI 文本绘制的实际长度上限，超长文本直接放弃烘制
            std::wstring ws = Gvtcier_W32_ToWide(to.text);
            if (!ws.empty() && ws.size() < 4095) {
                HDC wdc = GetDC(nullptr);
                // 字体固定 64 像素高、加粗、微软雅黑，保证 3D 文字在近处也清晰
                HFONT f = CreateFontW(64, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
                HDC mdc = CreateCompatibleDC(wdc);
                HGDIOBJ of = SelectObject(mdc, f);
                SIZE sz = { 0, 0 };
                GetTextExtentPoint32W(mdc, ws.c_str(), (int)ws.size(), &sz);
                // 四周各留 8 像素边，避免抗锯齿的边缘被裁；最小 4 像素防止空文本建零尺寸位图
                int tw = sz.cx + 16 > 4 ? sz.cx + 16 : 4;
                int th = sz.cy + 16 > 4 ? sz.cy + 16 : 4;
                BITMAPINFO bmi;
                memset(&bmi, 0, sizeof(bmi));
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = tw;
                // 高度写负数表示“自上而下”的位图，这样像素行序和 GL 纹理的 v 方向一致，
                // 贴到 3D 面上不会上下颠倒
                bmi.bmiHeader.biHeight = -th;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                HBITMAP bmp = CreateDIBSection(mdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (bmp && bits) {
                    HGDIOBJ ob = SelectObject(mdc, bmp);
                    // 白字 + 透明背景，稍后统一染色
                    SetTextColor(mdc, RGB(255, 255, 255));
                    SetBkMode(mdc, TRANSPARENT);
                    TextOutW(mdc, 8, 8, ws.c_str(), (int)ws.size());
                    GdiFlush();
                    unsigned char cr = (unsigned char)(to.lightColor.x * 255.0f + 0.5f);
                    unsigned char cg = (unsigned char)(to.lightColor.y * 255.0f + 0.5f);
                    unsigned char cb = (unsigned char)(to.lightColor.z * 255.0f + 0.5f);
                    std::vector<unsigned char> px((size_t)tw * th * 4);
                    const unsigned char* s = (const unsigned char*)bits;
                    for (int p = 0; p < tw * th; ++p) {
                        // DIB 是 BGRA 顺序，所以 [2]/[1]/[0] 分别是 R/G/B。
                        // Alpha 取三通道最大值就是字形的覆盖度（白字三通道相等，抗锯齿边缘会变暗）
                        unsigned char r8 = s[p * 4 + 2], g8 = s[p * 4 + 1], b8 = s[p * 4 + 0];
                        unsigned char a8 = r8 > g8 ? (r8 > b8 ? r8 : b8) : (g8 > b8 ? g8 : b8);
                        px[p * 4 + 0] = cr;
                        px[p * 4 + 1] = cg;
                        px[p * 4 + 2] = cb;
                        px[p * 4 + 3] = a8;
                    }
                    TextEntityTex et;
                    et.text = to.text;
                    et.r = to.lightColor.x; et.g = to.lightColor.y; et.b = to.lightColor.z;
                    et.w = tw; et.h = th;
                    // 纹理交给场景的 ID 系统托管，方便统一注销
                    ::Texture tmp;
                    if (tmp.create(tw, th, px.data())) {
                        et.tex = scene.Gvtcier_Scene_RegisterTexture(std::move(tmp));
                        textEntityTexs_[ck] = std::move(et);
                    }
                    SelectObject(mdc, ob);
                    DeleteObject(bmp);
                }
                DeleteDC(mdc);
                DeleteObject(f);
                ReleaseDC(nullptr, wdc);
            }
#endif
            it = textEntityTexs_.find(ck);
        }
        if (it != textEntityTexs_.end()) to.tex = it->second.tex;
    });

    // ---------- pass 11：卡通描边 ----------
    // 反向外壳法：先把每个物体沿法线在屏幕空间外扩一点点，用纯描边色画一遍，
    // 只保留正面（cullMode=Front 剔掉原本的正面，实际写进去的是外扩后的背面），
    // 然后再画正常几何把内部盖住，剩下的外扩边缘就是描边。
    // 因为要先于不透明几何绘制，所以放在这里。Effect 类与 Text 类不描边。
    if (toonOn_) {
        float ow = outlineW_;
        const float* oc = outlineCol_;
        outlineShader_.Gvtcier_Shader_Use();
        outlineShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
        outlineShader_.Gvtcier_Shader_SetMat4("uView", view.m);
        outlineShader_.Gvtcier_Shader_SetFloat("uOutlineWidth", ow);
        outlineShader_.Gvtcier_Shader_SetVec4("uOutlineColor", oc[0], oc[1], oc[2], 1.0f);
        {
            // 描边只写深度、剔除正面：这样外壳不会盖住物体本体，
            // 也不会和后面的正常几何互相打架
            PassState st;
            st.depthTest = true;
            st.depthWrite = true;
            st.depthFunc = Greater;
            st.cull = true;
            st.cullMode = Front;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = opt.wireframe ? Line : Fill;
            applyPassState(st);
        }
        for (int vi : visible) {
            Object tmpO;
            const Object* op = resolveObj(scene, objIndexBuf_[vi]);
            if (!op) continue;
            if (op->kind == ObjKind::Text) {
                // 文本对象本轮先跳过（下面 continue），这里先算出放大后的变换只用于统一处理
                tmpO = *op;
                tmpO.scale.x *= tmpO.textSize;
                tmpO.scale.y *= tmpO.textSize;
                tmpO.scale.z *= tmpO.textSize;
                op = &tmpO;
            }
            const Object& o = *op;
            if (o.kind == ObjKind::Effect || o.kind == ObjKind::EffectEmitter) continue;
            if (o.kind == ObjKind::Text) continue;
            Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
            if (!om) continue;
            outlineShader_.Gvtcier_Shader_SetMat4("uModel", modelMatrix(o).m);
            om->draw();
        }
        if (scene.hasSkinned() &&
            scene.skinnedBoneMats.size() >= scene.skinnedMesh().bones.size() * 16) {
            // 蒙皮网格的描边用带骨骼的专用着色器
            outlineSkinnedShader_.Gvtcier_Shader_Use();
            outlineSkinnedShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
            outlineSkinnedShader_.Gvtcier_Shader_SetMat4("uView", view.m);
            outlineSkinnedShader_.Gvtcier_Shader_SetFloat("uOutlineWidth", ow);
            outlineSkinnedShader_.Gvtcier_Shader_SetVec4("uOutlineColor", oc[0], oc[1], oc[2], 1.0f);
            Mat4 sm = Mat4::identity();
            outlineSkinnedShader_.Gvtcier_Shader_SetMat4("uModel", sm.m);
            outlineSkinnedShader_.Gvtcier_Shader_SetMat4Array("uBones", scene.skinnedBoneMats.data(),
                                        (int)scene.skinnedMesh().bones.size());
            scene.skinnedMesh().mesh.draw();
        }
        {
            PassState st;
            st.depthTest = true;
            st.depthWrite = true;
            st.depthFunc = Greater;
            st.cull = true;
            st.cullMode = Back;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = opt.wireframe ? Line : Fill;
            applyPassState(st);
        }
        mainShader_.Gvtcier_Shader_Use();
    }

    mainShader_.Gvtcier_Shader_SetInt("uTransparent", 0);

    // ---------- pass 12：不透明几何 ----------
    // 有硬件实例化支持时走“按 网格+贴图 分桶，一桶一次 drawInstanced”的路径，大幅减少 draw call；
    // 不支持时退化成逐个对象 draw。两条路径的筛选条件一致：
    // 跳过 Effect 类、跳过透明对象（留给后面的透明 pass）。
    const bool 支持实例化 = rhi::Gvtcier_Rhi_HasInstancing();
    if (!支持实例化) {
        for (int vi : visible) {
            const Object* op = resolveObj(scene, objIndexBuf_[vi]);
            if (!op) continue;
            const Object& o = *op;
            if (o.kind == ObjKind::Effect || o.kind == ObjKind::EffectEmitter) continue;
            if (o.transparent || o.alpha < 1.0f) continue;
            Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
            mainShader_.Gvtcier_Shader_SetVec3("uAlbedoTint", o.albedoTint.x, o.albedoTint.y, o.albedoTint.z);
            mainShader_.Gvtcier_Shader_SetInt("uSpecular", (specularOn_ && o.specular) ? 1 : 0);
            mainShader_.Gvtcier_Shader_SetInt("uMaterial", o.material);
            mainShader_.Gvtcier_Shader_SetFloat("uUvScale", o.uvScale);
            mainShader_.Gvtcier_Shader_SetMat4("uModel", modelMatrix(o).m);
            ::Texture* ot = scene.Gvtcier_Scene_GetTex(o.tex);
            const ::Texture* t6 = o.tex6.valid() ? scene.Gvtcier_Scene_GetTex(o.tex6) : nullptr;
            if (t6 && t6->id()) {
                mainShader_.Gvtcier_Shader_SetInt("uFaceTexOn", 1);
                mainShader_.Gvtcier_Shader_SetInt("uFaceTex", TexSlot::FaceTex);
                rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::FaceTex, Tex2DArray, t6->id());
            } else {
                mainShader_.Gvtcier_Shader_SetInt("uFaceTexOn", 0);
            }
            if (!om || !ot) continue;
            ot->bind(0);
            om->draw();
        }
        instBuckets_.clear();
        instOrder_.clear();
    } else {
    // 实例化路径第一步：把所有不透明对象按 网格 -> 贴图 -> 面纹理 排序，
    // 这样相同资源的对象会挨在一起，方便下面顺序扫描切桶。
    // 排序键带世代号是为了避免“已销毁对象释放的槽位被新对象复用”导致误判为同一资源。
    instBuckets_.clear();
    instOrder_.clear();
    for (int vi : visible) {
        const Object* op = resolveObj(scene, objIndexBuf_[vi]);
        if (!op) continue;
        const Object& o = *op;
        if (o.kind == ObjKind::Effect || o.kind == ObjKind::EffectEmitter) continue;
        if (o.transparent || o.alpha < 1.0f) continue;
        if (!scene.Gvtcier_Scene_GetMesh(o.mesh) || !scene.Gvtcier_Scene_GetTex(o.tex)) continue;
        instOrder_.push_back(&o);
    }
    std::sort(instOrder_.begin(), instOrder_.end(), [](const Object* a, const Object* b) {
        if (a->mesh != b->mesh) return a->mesh.index < b->mesh.index;
        if (a->mesh.generation != b->mesh.generation) return a->mesh.generation < b->mesh.generation;
        if (a->tex.index != b->tex.index) return a->tex.index < b->tex.index;
        if (a->tex.generation != b->tex.generation) return a->tex.generation < b->tex.generation;
        if (a->tex6.valid() != b->tex6.valid()) return (int)a->tex6.valid() < (int)b->tex6.valid();
        if (a->tex6.valid() && a->tex6.index != b->tex6.index) return a->tex6.index < b->tex6.index;
        if (a->tex6.valid() && a->tex6.generation != b->tex6.generation) return a->tex6.generation < b->tex6.generation;
        return false;
    });
    for (size_t k = 0; k < instOrder_.size();) {
        const Object* first = instOrder_[k];
        // 连续扫描出资源完全相同的区间 [k, e)，这一段就是一个实例化桶
        size_t e = k + 1;
        while (e < instOrder_.size() && instOrder_[e]->mesh == first->mesh &&
               instOrder_[e]->tex == first->tex && instOrder_[e]->tex6 == first->tex6) {
            ++e;
        }
        instBuckets_.push_back(InstBucket());
        InstBucket& bk = instBuckets_.back();
        bk.mesh = scene.Gvtcier_Scene_GetMesh(first->mesh);
        bk.tex = scene.Gvtcier_Scene_GetTex(first->tex);
        bk.tex6 = first->tex6.valid() ? scene.Gvtcier_Scene_GetTex(first->tex6) : nullptr;
        bk.objs.assign(instOrder_.begin() + k, instOrder_.begin() + e);
        {
            // 计算整个桶的屏幕包围矩形，用于尽可能开 scissor 裁掉屏幕外的片元。
            // 只要桶里有任何一个对象跨过相机平面（无法确定屏幕范围），就放弃 scissor 保证正确
            float ux0 = 1e30f, uy0 = 1e30f, ux1 = -1e30f, uy1 = -1e30f;
            bool allClip = true;
            for (const Object* op : bk.objs) {
                AABB3 bb = op->Gvtcier_Object_Bounds(scene.Gvtcier_Scene_MeshHalfExtents(op->mesh));
                Vec3 mn = aabbMin(bb);
                Vec3 mx = aabbMax(bb);
                bool allFront = true;
                for (int c = 0; c < 8; ++c) {
                    Vec3 p((c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z);
                    Vec4 clip = proj * view * Vec4(p.x, p.y, p.z, 1.0f);
                    if (clip.w <= 1e-5f) { allFront = false; break; }
                    float nx = clip.x / clip.w;
                    float ny = clip.y / clip.w;
                    if (nx < ux0) ux0 = nx;
                    if (ny < uy0) uy0 = ny;
                    if (nx > ux1) ux1 = nx;
                    if (ny > uy1) uy1 = ny;
                }
                if (!allFront) { allClip = false; break; }
            }
            if (allClip && ux1 > ux0 && uy1 > uy0) {
                // 2 像素外扩，避免光栅化边缘被 scissor 切掉
                const int kPad = 2;
                int px0 = (int)((ux0 * 0.5f + 0.5f) * (float)rw) - kPad;
                int py0 = (int)((uy0 * 0.5f + 0.5f) * (float)rh) - kPad;
                int px1 = (int)((ux1 * 0.5f + 0.5f) * (float)rw) + kPad;
                int py1 = (int)((uy1 * 0.5f + 0.5f) * (float)rh) + kPad;
                if (px0 < 0) px0 = 0;
                if (py0 < 0) py0 = 0;
                if (px1 > rw) px1 = rw;
                if (py1 > rh) py1 = rh;
                if (px1 > px0 && py1 > py0 &&
                    (px1 - px0) * (float)(py1 - py0) < (float)rw * (float)rh * 0.8f) {
                    // 只有覆盖面积小于全屏 80% 时开 scissor 才划算，否则额外状态切换反而更慢
                    bk.useScissor = true;
                    bk.sx = px0; bk.sy = py0; bk.sw = px1 - px0; bk.sh = py1 - py0;
                }
            }
        }
        k = e;
    }

    {
        static int dbgInst = 0;
        if (dbgInst++ < 3)
            rlog::once(dbgInst == 1 ? "dbg_inst1" : (dbgInst == 2 ? "dbg_inst2" : "dbg_inst3"),
                       "[3ddbg] instancing=%d instBuckets=%d vis=%d mainProg=%u instProg=%u\n",
                       (int)支持实例化, (int)instBuckets_.size(), (int)visible.size(),
                       mainShader_.Gvtcier_Shader_Id(), mainInstShader_.Gvtcier_Shader_Id());
    }
    if (!instBuckets_.empty()) {
        // 实例化路径第二步：切换实例化着色器并上传与对象无关的 uniform（与主着色器同一套）
        mainInstShader_.Gvtcier_Shader_Use();
        mainInstShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
        mainInstShader_.Gvtcier_Shader_SetMat4("uView", view.m);
        mainInstShader_.Gvtcier_Shader_SetVec3("uCamPos", cam.pos.x, cam.pos.y, cam.pos.z);
        mainInstShader_.Gvtcier_Shader_SetVec3("uLightDir", light.dir.x, light.dir.y, light.dir.z);
        mainInstShader_.Gvtcier_Shader_SetVec3("uLightColor", mainCol.x, mainCol.y, mainCol.z);
        mainInstShader_.Gvtcier_Shader_SetInt("uDirLightCount", (int)dirls.size());
        {
            bool csmOn = (scene.sunEnabled && opt.showShadow);
            uploadCsmUniforms(mainInstShader_, csmOn);
            uploadAsmUniforms(mainInstShader_, csmOn);
            if (csmOn) {
                rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::CSMShadow, Tex2DArray, sunShadow_.depthTex());
                rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AsmShadow, Tex2DArray, asmShadow_.depthTex());
            }
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube, TexCube, pointShadow_[0].cubeTex());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube2, TexCube, pointShadow_[1].cubeTex());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube3, TexCube, pointShadow_[2].cubeTex());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube4, TexCube, pointShadow_[3].cubeTex());
        }
        uploadDirLightUniforms(mainInstShader_, dirls, dircols);
        mainInstShader_.Gvtcier_Shader_SetInt("uDirShadowCount", 0);
        mainInstShader_.Gvtcier_Shader_SetFloat("uTime", (float)engineTime());
        mainInstShader_.Gvtcier_Shader_SetFloat("uAmbientMul", scene.ambientEnabled ? scene.ambientIntensity : 0.0f);
        mainInstShader_.Gvtcier_Shader_SetVec3("uAmbientColor", scene.ambientColor.x, scene.ambientColor.y, scene.ambientColor.z);
        mainInstShader_.Gvtcier_Shader_SetInt("uAlbedo", TexSlot::Albedo);
        mainInstShader_.Gvtcier_Shader_SetVec3("uFogColor", scene.skyColor.x, scene.skyColor.y, scene.skyColor.z);
        mainInstShader_.Gvtcier_Shader_SetFloat("uFogDensity", scene.fogDensity * fogMul);
        mainInstShader_.Gvtcier_Shader_SetInt("uSpecular", specularOn_ ? 1 : 0);
        mainInstShader_.Gvtcier_Shader_SetFloat("uShininess", 64.0f);
        mainInstShader_.Gvtcier_Shader_SetInt("uLightLimit", lightLimit_);
        mainInstShader_.Gvtcier_Shader_SetInt("uTileW", tileW_);
        mainInstShader_.Gvtcier_Shader_SetInt("uTileSize", Gvtcier_Limits().tileSize);
        mainInstShader_.Gvtcier_Shader_SetInt("uSun", 0);
        mainInstShader_.Gvtcier_Shader_SetInt("uFaceTex", TexSlot::FaceTex);

        for (size_t bi = 0; bi < instBuckets_.size(); ++bi) {
            InstBucket& bk = instBuckets_[bi];
            const int n = (int)bk.objs.size();
            if (n <= 0) continue;
            instScratch_.clear();
            instScratch_.reserve((size_t)n * 24);
            // 每个实例 24 个 float：4x4 模型矩阵 16 个 + 色调 3 个 + uvScale 1 个 +
            // 材质 1 个 + 是否文本 1 个 + 2 个保留位
            for (int k = 0; k < n; ++k) {
                const Object& o = *bk.objs[k];
                if (o.kind == ObjKind::Text) {
                    // 文本对象需要把 textSize 乘进缩放，所以单独拼一次矩阵，
                    // 不能直接用对象缓存的矩阵
                    Mat4 tm = Mat4::translate(o.pos)
                            * Mat4::rotateX(deg2rad(o.rotDeg.x))
                            * Mat4::rotateY(deg2rad(o.rotDeg.y))
                            * Mat4::rotateZ(deg2rad(o.rotDeg.z))
                            * Mat4::scale(Vec3(o.scale.x * o.textSize, o.scale.y * o.textSize, o.scale.z * o.textSize));
                    for (int f = 0; f < 16; ++f) instScratch_.push_back(tm.m[f]);
                } else {
                    const Mat4& mm = o.Gvtcier_Object_Matrix();
                    for (int f = 0; f < 16; ++f) instScratch_.push_back(mm.m[f]);
                }
                instScratch_.push_back(o.albedoTint.x);
                instScratch_.push_back(o.albedoTint.y);
                instScratch_.push_back(o.albedoTint.z);
                instScratch_.push_back(o.uvScale);
                instScratch_.push_back((float)o.material);
                instScratch_.push_back(o.kind == ObjKind::Text ? 1.0f : 0.0f);
                instScratch_.push_back(0.0f);
                instScratch_.push_back(0.0f);
            }
            bk.mesh->ensureInstanceCapacity(n);
            bk.mesh->uploadInstanceData(instScratch_.data(), n);
            {
                static int dbgBk = 0;
                if (dbgBk++ < 4)
                    rlog::once(dbgBk == 1 ? "dbg_bk1" : (dbgBk == 2 ? "dbg_bk2" : (dbgBk == 3 ? "dbg_bk3" : "dbg_bk4")),
                               "[3ddbg] bucket n=%d idx=%d m0=%.2f m5=%.2f m12=%.2f m13=%.2f m15=%.2f tint=%.2f uv=%.2f\n",
                               n, bk.mesh->indexCount(),
                               instScratch_[0], instScratch_[5], instScratch_[12], instScratch_[13],
                               instScratch_[15], instScratch_[16], instScratch_[19]);
            }
            bk.tex->bind(TexSlot::Albedo);
            // 有面纹理（表情/多帧图集）时额外绑一个 2D 纹理数组到 FaceTex 槽
            if (bk.tex6 && bk.tex6->id()) {
                mainInstShader_.Gvtcier_Shader_SetInt("uFaceTexOn", 1);
                rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::FaceTex, Tex2DArray, bk.tex6->id());
            } else {
                mainInstShader_.Gvtcier_Shader_SetInt("uFaceTexOn", 0);
            }
            if (bk.useScissor) {
                // 临时打开 scissor 把绘制限制在桶的屏幕矩形内，画完立刻关掉
                g_rs.scissor = true;
                g_rs.scissorX = bk.sx; g_rs.scissorY = bk.sy;
                g_rs.scissorW = bk.sw; g_rs.scissorH = bk.sh;
                rsFlush();
            }
            bk.mesh->drawInstanced(n);
            if (bk.useScissor) {
                g_rs.scissor = false;
                rsFlush();
            }
        }
        mainShader_.Gvtcier_Shader_Use();
    }
    }

    // ---------- pass 13：蒙皮网格 ----------
    // 场景里只有一套蒙皮模型（skinnedMesh），骨骼矩阵由动画系统每帧算好后放在 skinnedBoneMats。
    // 先校验矩阵数据长度够（骨骼数 × 16 个 float），否则说明动画还没准备好，跳过避免读越界。
    // 环境光这里写死 0.15/0.15/0.18（偏蓝的冷环境色）而不是用场景的环境光，
    // 这是蒙皮着色器的特殊处理，偏蓝是为了让角色暗部不死黑
    if (scene.hasSkinned() &&
        scene.skinnedBoneMats.size() >= scene.skinnedMesh().bones.size() * 16) {
        skinnedShader_.Gvtcier_Shader_Use();
        skinnedShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
        skinnedShader_.Gvtcier_Shader_SetMat4("uView", view.m);
        skinnedShader_.Gvtcier_Shader_SetVec3("uLightDir", light.dir.x, light.dir.y, light.dir.z);
        skinnedShader_.Gvtcier_Shader_SetVec3("uLightColor", mainCol.x, mainCol.y, mainCol.z);
        skinnedShader_.Gvtcier_Shader_SetVec3("uAmbient", (scene.ambientEnabled ? 0.15f : 0.0f), (scene.ambientEnabled ? 0.15f : 0.0f), (scene.ambientEnabled ? 0.18f : 0.0f));
        skinnedShader_.Gvtcier_Shader_SetInt("uTex", 0);
        const ::Texture* st = scene.Gvtcier_Scene_GetSkinnedTex();
        if (st && st->id()) st->bind(0);
        else if (::Texture* wt = scene.Gvtcier_Scene_GetTex(scene.whiteTex())) wt->bind(0);
        skinnedShader_.Gvtcier_Shader_SetVec4("uBaseColor",
            scene.skinnedMesh().baseColorFactor[0], scene.skinnedMesh().baseColorFactor[1],
            scene.skinnedMesh().baseColorFactor[2], scene.skinnedMesh().baseColorFactor[3]);
        skinnedShader_.Gvtcier_Shader_SetMat4Array("uBones", scene.skinnedBoneMats.data(),
                                    (int)scene.skinnedMesh().bones.size());
        scene.skinnedMesh().mesh.draw();
        mainShader_.Gvtcier_Shader_Use();
    }

    // ---------- pass 15：调试线框 ----------
    // 调试碰撞体线框（opt.debugCollider 控制）每次都用当前相机现算 VP 矩阵；
    // 世界线是脚本每帧追加的一次性线段，画完就清空，所以只画这一帧。
    if (opt.debugCollider && !dbgLines_.empty()) {
        Mat4 dbgProj = cam.projMatrix((float)opt.width / (float)opt.height);
        Mat4 dbgView = cam.viewMatrix();
        drawDebugLines(dbgProj * dbgView, dbgLines_);
    }

    if (!worldLines_.empty()) {
        Mat4 wlProj = cam.projMatrix((float)opt.width / (float)opt.height);
        Mat4 wlView = cam.viewMatrix();
        drawDebugLines(wlProj * wlView, worldLines_);
        worldLines_.clear();
    }

    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.depthFunc = Greater;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    tickMark(tGeom);
    // ---------- pass 16：buildHzb ----------
    // 注意这里是拿本帧已经画好的深度去更新 HZB，供“下一帧”的遮挡剔除用，属于滞后一帧的策略
    buildHzb(scene, cam, opt);
    // ---------- pass 17：GTAO ----------
    if (opt.gtao) {
        gtaoPass(scene, cam, opt, rw, rh);
    } else {
        gtaoAOTex_ = 0;
    }
    tickMark(tGtao);
    // ---------- pass 18：延迟光照 ----------
    // 全屏 pass：读 G-Buffer 的法线/反照率/材质/深度，重新算一遍所有光照（太阳 + CSM 阴影 + tile 点光），
    // 结果覆盖写回 HDR 的 attachment0。之所以叫“延迟”，是因为光照不依赖原始几何，
    // 只依赖 G-Buffer，所以可以用一个全屏 quad 一次算完，与场景几何量无关。
    // 绘制前把颜色输出临时限制为仅 attachment0，避免光照结果写进法线/反照率附件把它们覆盖掉，
    // 画完再恢复四路输出。
    if (deferredLightShader_.Gvtcier_Shader_Id() != 0) {
        Mat4 viewProj = proj * view;
        Mat4 invViewProj = viewProj.inverse();
        {
            PassState st;
            st.depthTest = false;
            st.depthWrite = false;
            st.cull = false;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = Fill;
            applyPassState(st);
        }
        rhi::Gvtcier_Rhi_Viewport(0, 0, rw, rh);
        deferredLightShader_.Gvtcier_Shader_Use();
        deferredLightShader_.Gvtcier_Shader_SetInt("uGNormal", TexSlot::GNormal);
        deferredLightShader_.Gvtcier_Shader_SetInt("uGAlbedo", TexSlot::GAlbedo);
        deferredLightShader_.Gvtcier_Shader_SetInt("uGMaterial", TexSlot::GMaterial);
        deferredLightShader_.Gvtcier_Shader_SetInt("uDepth", TexSlot::Depth);
        deferredLightShader_.Gvtcier_Shader_SetMat4("uInvViewProj", invViewProj.m);
        deferredLightShader_.Gvtcier_Shader_SetMat4("uVP", viewProj.m);
        float csmLB = kCsmDefaults.lightBias;
        float csmNB = kCsmDefaults.normalBias;
        float csmSC = kCsmDefaults.slopeClamp;
        float csmDR = kCsmDefaults.diskRadius;
        float csmPR = kCsmDefaults.pcfRadius;
        float csmZP = kCsmDefaults.zPad;
        float csmBS = kCsmDefaults.biasScale;
        float csmBB = kCsmDefaults.blendBand;
        int csmDebug = 0;
        int csmDebugLayer = 0;
        int csmSingleTap = 0;
        int csmShowGrid = 0;
        int csmShowWorldTexel = 0;
#if GVTCIER_CSM_TUNING
        const ShadowTuning& st = Gvtcier_Shadow_Tuning();
        csmLB = st.lightBias;
        csmNB = st.normalBias;
        csmSC = st.slopeClamp;
        csmDR = st.diskRadius;
        csmPR = st.pcfRadius;
        csmZP = st.zPad;
        csmBS = st.biasScale;
        csmBB = st.blendBand;
        csmDebug = st.debug;
        csmDebugLayer = st.debugLayer;
        csmSingleTap = st.singleTap;
        csmShowGrid = st.showTexelGrid;
        csmShowWorldTexel = st.showWorldTexel;
#endif
        deferredLightShader_.Gvtcier_Shader_SetInt("uCSMDebug", csmDebug);
        deferredLightShader_.Gvtcier_Shader_SetVec4("uCSMTune", csmLB, csmNB, csmSC, csmDR);
        deferredLightShader_.Gvtcier_Shader_SetVec4("uCSMTune2", csmPR, csmZP, csmBS, csmBB);
        deferredLightShader_.Gvtcier_Shader_SetInt("uSingleTap", csmSingleTap);
        deferredLightShader_.Gvtcier_Shader_SetInt("uDebugLayer", csmDebugLayer);
        deferredLightShader_.Gvtcier_Shader_SetInt("uShowTexelGrid", csmShowGrid);
        deferredLightShader_.Gvtcier_Shader_SetInt("uShowWorldTexel", csmShowWorldTexel);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uShadowSize", (float)sunShadow_.size());
        deferredLightShader_.Gvtcier_Shader_SetInt("uAO", TexSlot::AODepth);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uAOOn", gtaoAOTex_ ? 1.0f : 0.0f);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uAOFade", 1.0f);
        deferredLightShader_.Gvtcier_Shader_SetMat4("uView", view.m);
        deferredLightShader_.Gvtcier_Shader_SetVec3("uCamPos", cam.pos.x, cam.pos.y, cam.pos.z);
        deferredLightShader_.Gvtcier_Shader_SetVec3("uLightDir", light.dir.x, light.dir.y, light.dir.z);
        deferredLightShader_.Gvtcier_Shader_SetVec3("uLightColor", mainCol.x, mainCol.y, mainCol.z);
        deferredLightShader_.Gvtcier_Shader_SetInt("uSun", scene.sunEnabled ? 1 : 0);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uSpecular", specularOn_ ? 1.0f : 0.0f);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uLightLimit", (float)lightLimit_);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uShininess", 64.0f);
        deferredLightShader_.Gvtcier_Shader_SetInt("uAlphaTest", 0);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uAmbientMul", scene.ambientEnabled ? scene.ambientIntensity : 0.0f);
        deferredLightShader_.Gvtcier_Shader_SetVec3("uAmbientColor", scene.ambientColor.x, scene.ambientColor.y, scene.ambientColor.z);
        deferredLightShader_.Gvtcier_Shader_SetVec3("uFogColor", scene.skyColor.x, scene.skyColor.y, scene.skyColor.z);
        deferredLightShader_.Gvtcier_Shader_SetFloat("uFogDensity", scene.fogDensity * fogMul);
        deferredLightShader_.Gvtcier_Shader_SetInt("uTileW", tileW_);
        deferredLightShader_.Gvtcier_Shader_SetInt("uTileSize", kTileSize);
        deferredLightShader_.Gvtcier_Shader_SetInt("uDirLightCount", (int)dirls.size());
        uploadDirLightUniforms(deferredLightShader_, dirls, dircols);
        deferredLightShader_.Gvtcier_Shader_SetInt("uDirShadowCount", 0);
        bool csmOn = (scene.sunEnabled && opt.showShadow && sunShadow_.layers() >= 4);
        uploadCsmUniforms(deferredLightShader_, csmOn);
        uploadAsmUniforms(deferredLightShader_, csmOn);
        {
            static int dbgCsmFrame = 0;
            if (++dbgCsmFrame == 60 || dbgCsmFrame == 600) {
                rlog::once(dbgCsmFrame == 60 ? "dbg_csm60" : "dbg_csm600",
                           "[csm] f=%d on=%d splits=%.2f,%.2f,%.2f,%.2f texel=%.4f,%.4f,%.4f,%.4f range=%.2f,%.2f,%.2f,%.2f anchor=%.1f,%.1f,%.1f csmVP0=%.2f,%.2f,%.2f,%.2f\n",
                           dbgCsmFrame, (int)csmOn,
                           csmSplits_[0], csmSplits_[1], csmSplits_[2], csmSplits_[3],
                           csmTexel_[0], csmTexel_[1], csmTexel_[2], csmTexel_[3],
                           csmRange_[0], csmRange_[1], csmRange_[2], csmRange_[3],
                           csmAnchor_.x, csmAnchor_.y, csmAnchor_.z,
                           (float)csmVP_[0].m[0], (float)csmVP_[0].m[5],
                           (float)csmVP_[0].m[10], (float)csmVP_[0].m[15]);
            }
        }
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 0, lightSsbo_);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 1, tileSsbo_);
        rhi::Gvtcier_Rhi_BindBufferBase(ShaderStorageBuffer, 2, idxSsbo_);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::GNormal, Tex2D, hdrFbo_.normalTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::GAlbedo, Tex2D, hdrFbo_.albedoTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::GMaterial, Tex2D, hdrFbo_.materialTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Depth, Tex2D, hdrFbo_.depthTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AODepth, Tex2D, gtaoAOTex_ ? gtaoAOTex_ : hdrFbo_.colorTex());
        if (scene.sunEnabled && opt.showShadow && sunShadow_.layers() >= 4) {
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::CSMShadow, Tex2DArray, sunShadow_.depthTex());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AsmShadow, Tex2DArray, asmShadow_.depthTex());
        }
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube, TexCube, pointShadow_[0].cubeTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube2, TexCube, pointShadow_[1].cubeTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube3, TexCube, pointShadow_[2].cubeTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::PointShadowCube4, TexCube, pointShadow_[3].cubeTex());
        hdrFbo_.bind();
        rhi::Gvtcier_Rhi_DrawBuffer(ColorAttachment0);
        fullscreenQuad_.draw();
        {
            const unsigned bufs[4] = { ColorAttachment0, ColorAttachment1, ColorAttachment2, ColorAttachment3 };
            rhi::Gvtcier_Rhi_DrawBuffers(4, bufs);
        }
        {
            PassState st;
            st.depthTest = true;
            st.depthWrite = true;
            st.depthFunc = Greater;
            st.cull = true;
            st.cullMode = Back;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = opt.wireframe ? Line : Fill;
            applyPassState(st);
        }
    }

    // ---------- pass 19：阴影调试探针 ----------
    // debug >= 9 才读像素，正常游戏不会走到
#if GVTCIER_CSM_TUNING
    if (Gvtcier_Shadow_Tuning().debug >= 9) shadowProbePass(cam);
#endif

    // ---------- pass 20：特效粒子 ----------
    // 必须排在延迟光照之后：延迟光照是一张全屏 pass，会把先前写进 HDR 的颜色整片覆盖，
    // 特效和透明几何一样，只有画在它后面才能留在画面上。
    //
    // 粒子四边形由顶点着色器按 gl_VertexID 展开，CPU 每颗粒子只提交一份数据。
    // 光束两端点不同，仍在这里展开，所以需要相机右向量。
    // 分两批：
    //   v      —— 加色混合粒子与光束（blendSrc=SrcAlpha, blendDst=One，纯发光叠加）
    //   vSmoke —— alphaBlend 粒子（常规 alpha 混合，并且写深度），用于烟雾这类遮挡物
    // 先画加色批再画烟雾批，保证烟雾能盖住后面的光
    if (!fx_.particles().empty() || !fx_.beams().empty()) {
        Vec3 right(view.m[0], view.m[4], view.m[8]);

        std::vector<FxVertex> v;
        std::vector<FxVertex> vSmoke;
        v.reserve((fx_.particles().size() + fx_.beams().size() * 2) * 6);
        vSmoke.reserve(fx_.particles().size() * 6);

        for (const FxParticle& p : fx_.particles()) {
            float fade = p.life / p.maxLife;
            if (fade < 0.0f) fade = 0.0f;
            if (fade > 1.0f) fade = 1.0f;
            float a = p.color.a * (p.alphaBlend ? 1.0f : fade);
            float age = 1.0f - fade;
            FxVertex vtx = {
                { p.pos.x, p.pos.y, p.pos.z },
                p.color.r, p.color.g, p.color.b, a,
                p.size, p.rotation,
                { age, (float)p.kind },
                (float)p.facing,
                { p.vel.x, p.vel.y, p.vel.z },
            };
            std::vector<FxVertex>& dst = p.alphaBlend ? vSmoke : v;
            for (int k = 0; k < 6; ++k) dst.push_back(vtx);
        }
        for (const FxBeam& b : fx_.beams()) {
            Vec3 off = right * b.width;
            FxVertex q[4] = {
                { { b.a.x - off.x, b.a.y - off.y, b.a.z - off.z }, b.color.r, b.color.g, b.color.b, b.color.a, -1.0f, 0.0f, { 0.0f, 2.0f }, 3.0f, { 0.0f, 0.0f, 0.0f } },
                { { b.a.x + off.x, b.a.y + off.y, b.a.z + off.z }, b.color.r, b.color.g, b.color.b, b.color.a, -1.0f, 0.0f, { 0.0f, 2.0f }, 3.0f, { 0.0f, 0.0f, 0.0f } },
                { { b.b.x + off.x, b.b.y + off.y, b.b.z + off.z }, b.color.r, b.color.g, b.color.b, b.color.a, -1.0f, 0.0f, { 0.0f, 2.0f }, 3.0f, { 0.0f, 0.0f, 0.0f } },
                { { b.b.x - off.x, b.b.y - off.y, b.b.z - off.z }, b.color.r, b.color.g, b.color.b, b.color.a, -1.0f, 0.0f, { 0.0f, 2.0f }, 3.0f, { 0.0f, 0.0f, 0.0f } },
            };
            v.push_back(q[0]); v.push_back(q[1]); v.push_back(q[2]);
            v.push_back(q[0]); v.push_back(q[2]); v.push_back(q[3]);
        }

        if (!v.empty() || !vSmoke.empty()) {
            {
                PassState st;
                st.depthTest = true;
                st.depthWrite = false;
                st.depthFunc = Greater;
                st.cull = false;
                st.frontFace = Ccw;
                st.blend = true;
                st.blendSrc = SrcAlpha;
                st.blendDst = One;
                st.scissor = false;
                st.polygonMode = Fill;
                applyPassState(st);
            }
            fxShader_.Gvtcier_Shader_Use();
            fxShader_.Gvtcier_Shader_SetMat4("uProj", proj.m);
            fxShader_.Gvtcier_Shader_SetMat4("uView", view.m);
            fxShader_.Gvtcier_Shader_SetVec3("uCamPos", cam.pos.x, cam.pos.y, cam.pos.z);
            rhi::Gvtcier_Rhi_BindVertexArray(fxVao_);
            rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, fxVbo_);
            if (!v.empty()) {
                rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(v.size() * sizeof(FxVertex)), v.data(), DynamicDraw);
                rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)v.size());
            }
            if (!vSmoke.empty()) {
                g_rs.blendSrc = SrcAlpha; g_rs.blendDst = OneMinusSrcAlpha;
                g_rs.depthWrite = false;
                rsFlush();
                rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(vSmoke.size() * sizeof(FxVertex)), vSmoke.data(), DynamicDraw);
                rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)vSmoke.size());
                g_rs.depthWrite = false;
                g_rs.blendSrc = SrcAlpha; g_rs.blendDst = One;
                rsFlush();
            }
            rhi::Gvtcier_Rhi_BindVertexArray(0);
            {
                PassState st;
                st.depthTest = true;
                st.depthWrite = true;
                st.depthFunc = Greater;
                st.cull = true;
                st.cullMode = Back;
                st.frontFace = Ccw;
                st.blend = false;
                st.scissor = false;
                st.polygonMode = opt.wireframe ? Line : Fill;
                applyPassState(st);
            }
        }
    }

    // ---------- pass 21：透明几何 ----------
    // 先扫一遍可见列表，确认到底有没有透明对象，没有就整段跳过，省掉状态切换。
    // 透明物体开 alpha 混合、深度测试开但深度写入关：
    // 不写深度是为了让后面的透明物体能叠上来，避免“近处透明把远处透明挡住”。
    // 绘制顺序沿用 visible 列表顺序（未按距离排序），所以重叠的透明物体可能有排序问题。
    {
        bool anyTransparent = false;
        for (int vi : visible) {
            const Object* op = resolveObj(scene, objIndexBuf_[vi]);
            if (!op) continue;
            if ((op->transparent || op->alpha < 1.0f) && op->kind != ObjKind::Effect && op->kind != ObjKind::EffectEmitter) {
                anyTransparent = true;
                break;
            }
        }
        if (anyTransparent) {
            hdrFbo_.bind();
            rhi::Gvtcier_Rhi_Viewport(0, 0, rw, rh);
            {
                PassState st;
                st.depthTest = true;
                st.depthWrite = false;
                st.depthFunc = Greater;
                st.cull = true;
                st.cullMode = Back;
                st.frontFace = Ccw;
                st.blend = true;
                st.blendSrc = SrcAlpha;
                st.blendDst = OneMinusSrcAlpha;
                st.scissor = false;
                st.polygonMode = opt.wireframe ? Line : Fill;
                applyPassState(st);
            }
            mainShader_.Gvtcier_Shader_Use();
            mainShader_.Gvtcier_Shader_SetInt("uTransparent", 1);
            for (int vi : visible) {
                Object tmpO;
                const Object* op = resolveObj(scene, objIndexBuf_[vi]);
                if (!op) continue;
                if (op->kind == ObjKind::Text) {
                    tmpO = *op;
                    tmpO.scale.x *= tmpO.textSize;
                    tmpO.scale.y *= tmpO.textSize;
                    tmpO.scale.z *= tmpO.textSize;
                    op = &tmpO;
                }
                const Object& o = *op;
                if (!o.transparent && o.alpha >= 1.0f) continue;
                if (o.kind == ObjKind::Effect || o.kind == ObjKind::EffectEmitter) continue;
                mainShader_.Gvtcier_Shader_SetInt("uAlphaTest", o.kind == ObjKind::Text ? 1 : 0);
                // 非物理驱动的物体（静态装饰）用光照贴图，动态物体不用，避免光照贴图错位
                mainShader_.Gvtcier_Shader_SetInt("uUseLightmap", (!o.rigidBody && !o.kinematic) ? 1 : 0);
                mainShader_.Gvtcier_Shader_SetVec3("uAlbedoTint", o.albedoTint.x, o.albedoTint.y, o.albedoTint.z);
                mainShader_.Gvtcier_Shader_SetFloat("uAlpha", o.alpha);
                mainShader_.Gvtcier_Shader_SetInt("uSpecular", (specularOn_ && o.specular) ? 1 : 0);
                mainShader_.Gvtcier_Shader_SetInt("uMaterial", o.material);
                mainShader_.Gvtcier_Shader_SetFloat("uUvScale", o.uvScale);
                mainShader_.Gvtcier_Shader_SetMat4("uModel", modelMatrix(o).m);
                const ::Texture* t6 = o.tex6.valid() ? scene.Gvtcier_Scene_GetTex(o.tex6) : nullptr;
                if (t6 && t6->id()) {
                    mainShader_.Gvtcier_Shader_SetInt("uFaceTexOn", 1);
                    mainShader_.Gvtcier_Shader_SetInt("uFaceTex", TexSlot::FaceTex);
                    rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::FaceTex, Tex2DArray, t6->id());
                } else {
                    mainShader_.Gvtcier_Shader_SetInt("uFaceTexOn", 0);
                }
                Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
                ::Texture* ot = scene.Gvtcier_Scene_GetTex(o.tex);
                if (!om || !ot) continue;
                ot->bind(0);
                om->draw();
            }
            mainShader_.Gvtcier_Shader_SetInt("uTransparent", 0);
            {
                PassState st;
                st.depthTest = true;
                st.depthWrite = true;
                st.depthFunc = Greater;
                st.cull = true;
                st.cullMode = Back;
                st.frontFace = Ccw;
                st.blend = false;
                st.scissor = false;
                st.polygonMode = opt.wireframe ? Line : Fill;
                applyPassState(st);
            }
        }
    }

    // ---------- pass 21：太阳阴影 ----------
    // 放在透明几何之后，是因为阴影图只关心不透明物体的遮挡关系，透明物体投影意义不大。
    // 这里只生成 csmVP_ 等矩阵并填阴影图，实际采样发生在下一帧的延迟光照 pass 里，
    // 所以在屏幕上看到的阴影会落后一帧。
    if (opt.showShadow) sunShadowPass(scene, cam, sunDirW, opt);
    if (opt.showShadow && pointShadowCount_ > 0) {
        static const Vec3 kFaceDir[6] = { Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1) };
        static const Vec3 kFaceUp[6] = { Vec3(0, -1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(0, -1, 0), Vec3(0, -1, 0) };
        pointDepthShader_.Gvtcier_Shader_Use();
        for (int pi = 0; pi < pointShadowCount_; ++pi) {
            const Vec3& lp = pointShadowPos_[pi];
            float farD = pointShadowFar_[pi];
            for (int f = 0; f < 6; ++f) {
                pointShadow_[pi].bindFace(f);
                PassState st;
                st.depthTest = true;
                st.depthWrite = true;
                st.depthFunc = Greater;
                st.cull = true;
                st.cullMode = Front;
                st.frontFace = Ccw;
                st.blend = false;
                st.scissor = false;
                st.polygonMode = Fill;
                applyPassState(st);
                Mat4 fv = Mat4::lookAt(Vec3(0.0f, 0.0f, 0.0f), kFaceDir[f], kFaceUp[f]);
                Mat4 fp = Mat4::perspective(90.0f, 1.0f, 0.05f, farD, Conventions::active());
                Mat4 fvp = fp * fv;
                pointDepthShader_.Gvtcier_Shader_SetMat4("uLightVP", fvp.m);
                pointDepthShader_.Gvtcier_Shader_SetVec3("uShadowAnchor", lp.x, lp.y, lp.z);
                scene.ForEachObject([&](const Object& o, GvtId<Object>) {
                    Mesh* om = scene.Gvtcier_Scene_GetMesh(o.mesh);
                    if (!o.visible || !o.castShadow || !om) return;
                    pointDepthShader_.Gvtcier_Shader_SetMat4("uModel", modelMatrix(o).m);
                    om->draw();
                });
            }
        }
        pointShadow_[0].unbind(viewW_, viewH_);
    }
    // ---------- pass 22：体积阴影 + 体积光 ----------
    // 只有雾等级 >= 2 且太阳开启时才做体积光；体积阴影是它的前置输入
    if (fogLevel_ >= 2 && scene.sunEnabled) {
        volumetricShadowPass(scene);
        volumetricPass(scene, cam, opt);
    }
    tickMark(tVol);
    // 几何与体积光都完事，接下来是后处理（含太阳阴影、Bloom、曝光）
    phase_ = 3;
    if (Gvtcier_StageOver()) return;

    // ---------- pass 23：Bloom + 自动曝光 + compose + FXAA ----------
    // hdr 为 false 时整条 HDR 后处理链跳过，直接 compose 到屏幕（曝光固定 1、无 bloom）。
    if (opt.hdr) {
        bloomMips_[0].bind();
        {
            PassState st;
            st.depthTest = false;
            st.depthWrite = false;
            st.cull = false;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = Fill;
            applyPassState(st);
        }
        brightShader_.Gvtcier_Shader_Use();
        brightShader_.Gvtcier_Shader_SetInt("uScene", TexSlot::Scene);
        // Bloom 亮度阈值：只有亮度超过它的像素才参与发光。
        // 开启体积光时用 0.35，因为体积光本身会把画面整体提亮，阈值高了会发不出光晕；
        // 关闭体积光时用 0.8，只有真正的高光（金属反光、激光）才会泛光，避免画面糊成一片
        // 用「体积光开关」来决定 Bloom 阈值，这属于把两个八竿子打不着的
        // 旋钮用胶带粘在一起。关掉体积光，Bloom 阈值从 0.35 跳到 0.8，
        // 画面亮度会莫名其妙变一截。谁要是顺着名字去查这个字段，
        // 大概率会在这个文件里发出一声「啊？」。
        brightShader_.Gvtcier_Shader_SetFloat("uThreshold", scene.volumeLightEnabled ? 0.35f : 0.8f);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, hdrFbo_.colorTex());
        fullscreenQuad_.draw();
        blurShader_.Gvtcier_Shader_Use();
        blurShader_.Gvtcier_Shader_SetInt("uSrc", TexSlot::Src);
        blurShader_.Gvtcier_Shader_SetFloat("uIntensity", 1.0f);
        for (int i = 1; i < 4; ++i) {
            // 降采样链：每级把上一级模糊并缩小一半，形成 4 级金字塔
            bloomMips_[i].bind();
            blurShader_.Gvtcier_Shader_SetVec2("uTexel", 1.0f / (float)bloomMips_[i - 1].width(), 1.0f / (float)bloomMips_[i - 1].height());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, bloomMips_[i - 1].colorTex());
            fullscreenQuad_.draw();
        }
        g_rs.blend = true;
        // 升采样叠加用 One/One（纯加色）：各级光晕直接相加，得到宽泛的外发光
        g_rs.blendSrc = One; g_rs.blendDst = One;
        rsFlush();
        // 升采样时每一级叠加的权重：越大的层级（越模糊）权重越低，
        // 0.40 / 0.16 / 0.05 逐级递减约 2.5 倍，形成“近处亮、外圈淡”的自然衰减
        static const float kBloomUp[3] = { 0.40f, 0.16f, 0.05f };
        for (int i = 3; i >= 1; --i) {
            bloomMips_[i - 1].bind();
            blurShader_.Gvtcier_Shader_SetFloat("uIntensity", kBloomUp[i - 1]);
            blurShader_.Gvtcier_Shader_SetVec2("uTexel", 1.0f / (float)bloomMips_[i].width(), 1.0f / (float)bloomMips_[i].height());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, bloomMips_[i].colorTex());
            fullscreenQuad_.draw();
        }
        g_rs.blend = false;
        rsFlush();
        // 以下是自动曝光：
        // 思路是把整屏 HDR 颜色降采样到一张 1x1 的图，得到“场景平均亮度”，
        // 再按 0.18 / 平均亮度 算出曝光系数，让平均亮度最终映射到中灰。
        // 为了省掉每帧同步回读的开销，三个手段：
        //   1. 降采样在着色器里完成（1x1 目标 = 全屏平均）
        //   2. 每 20 帧才采样一次（亮度变化慢，人眼看不出）
        //   3. 用 PBO 异步回读：这帧发起读取，下一帧再取结果，不阻塞 CPU
        double nowT = Gvtcier_NowSeconds();
        // 帧间隔 eDt 限制在 0~0.2 秒：切后台回来会有超大的间隔，夹住避免曝光瞬间跳变
        float eDt = (exposureLastTime_ > 0.0) ? (float)(nowT - exposureLastTime_) : 0.016f;
        exposureLastTime_ = nowT;
        if (eDt < 0.0f) eDt = 0.0f;
        if (eDt > 0.2f) eDt = 0.2f;
        if (lumFbo_.width() != 1 || lumFbo_.height() != 1) lumFbo_.create(1, 1, false);
        static int lumFrame = 0;
        // 每 20 帧采样一次平均亮度
        bool lumProbe = autoExposureOn_ && (lumFrame++ % 20 == 0);
        if (lumProbe) {
            lumFbo_.bind();
            lumShader_.Gvtcier_Shader_Use();
            lumShader_.Gvtcier_Shader_SetInt("uScene", TexSlot::Scene);
            lumShader_.Gvtcier_Shader_SetInt("uDepth", TexSlot::Depth);
            // uMaxLum 4.0：亮度上限，超过的部分按 4 截断，防止太阳/激光这种极亮像素
            // 把平均亮度拉高导致整个画面变暗（相当于抗饱和保护）
            lumShader_.Gvtcier_Shader_SetFloat("uMaxLum", 4.0f);
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, hdrFbo_.colorTex());
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Depth, Tex2D, hdrFbo_.depthTex());
            fullscreenQuad_.draw();
            if (gvPboMapSupported()) {
                // 用双 PBO 发起异步读取，16 字节 = 1 个 RGBA32F 像素。
                // 0x1902/0x1406 分别是 RGBA 与 FLOAT
                if (probePbo_[0] == 0) rhi::Gvtcier_Rhi_GenBuffers(2, probePbo_);
                int li = 1 - probePboIndex_;
                rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, probePbo_[li]);
                rhi::Gvtcier_Rhi_BufferData(rhi::PixelPackBuffer, 16, nullptr, rhi::StreamRead);
                gvReadPixels(0, 0, 1, 1, 0x1902, 0x1406, nullptr);
                lumPboPending_ = true;
                lumPboIndex_ = li;
                rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, 0);
            }
        }
        if (lumPboPending_) {
            // 取回上一次发起的异步读取结果
            rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, probePbo_[lumPboIndex_]);
            const void* mapped = gvMapPixelBuffer(16);
            if (mapped) {
                float lum[4];
                memcpy(lum, mapped, sizeof(lum));
                gvUnmapPixelBuffer();
                // 过滤异常值：太小说明还没写完，太大说明是 inf/NaN
                if (lum[0] > 1e-4f && lum[0] < 1e6f) {
                    // 首次拿到有效值直接作为初值，避免第一帧从默认 0.26 慢慢爬上去
                    if (!exposureInit_) { exposureSmooth_ = lum[0]; exposureInit_ = true; }
                    // 非对称收敛：从亮变暗时用 2.5 倍速度（进入阴影反应快），
                    // 从暗变亮时用 1 倍（走出阴影慢慢变亮，更像人眼适应）
                    float speed = (lum[0] < exposureSmooth_) ? 2.5f : 1.0f;
                    // 用 eDt * speed 做指数平滑，一帧实际采样间隔约 20 帧，所以 eDt 是真实时间差
                    exposureSmooth_ += (lum[0] - exposureSmooth_) * std::min(1.0f, eDt * speed);
                }
            }
            rhi::Gvtcier_Rhi_BindBuffer(rhi::PixelPackBuffer, 0);
            lumPboPending_ = false;
        }
        // 曝光 = 0.18 / 场景平均亮度。0.18 是摄影里的中灰锚点（18% 灰，
        // 人眼认为“中等亮度”的反射率），这样场景平均亮度会被映射到中灰。
        // 分母下限取 0.02：全黑场景下平均亮度可能是 0，除出来无穷大会让整画面爆白
        float exposure = 0.18f / std::max(exposureSmooth_, 0.02f);
        // 曝光系数夹在 0.20~2.5：防止极端场景把画面压到全黑或提亮到全白
        exposure = std::min(std::max(exposure, 0.20f), 2.5f);
        // 关掉自动曝光时固定 1.0（即不做任何曝光调整）
        if (!autoExposureOn_) exposure = 1.0f;
        // 再乘用户手动调节的倍率
        exposure *= exposureMul_;
        if (fxaaOn_) {
            // 开 FXAA 时先输出到 LDR 缓冲，末尾再跑 FXAA 到屏幕
            ldrFbo_.bind();
        } else {
            // 不开 FXAA 就直接进默认帧缓冲（屏幕）
            rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
            rhi::Gvtcier_Rhi_Viewport(0, 0, opt.width, opt.height);
        }
        {
            PassState st;
            st.depthTest = false;
            st.depthWrite = false;
            st.cull = false;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = Fill;
            applyPassState(st);
        }
        composeShader_.Gvtcier_Shader_Use();
        composeShader_.Gvtcier_Shader_SetInt("uScene", TexSlot::Scene);
        composeShader_.Gvtcier_Shader_SetInt("uBloom", TexSlot::Bloom);
        composeShader_.Gvtcier_Shader_SetInt("uAO", TexSlot::AODepth);
        composeShader_.Gvtcier_Shader_SetInt("uVolume", TexSlot::Volume);
        composeShader_.Gvtcier_Shader_SetInt("uDepthTex", TexSlot::Depth);
        composeShader_.Gvtcier_Shader_SetFloat("uAOOn", gtaoAOTex_ ? 1.0f : 0.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uAOFade", 0.35f);
        // 0.22 是 Bloom 在最终画面里的混合强度：光晕只做点缀，
        // 太高会让人感觉画面“发光过度”看不清内容，太低又看不出体积光
        // 这个 0.22 是写死的。设置界面里那个「泛光强度」滑块一路传到
        // scene.bloomStrength，然后就没有然后了 —— 全仓库没有任何一行读它。
        // 用户在设置里拖了半天，画面纹丝不动，堪称最礼貌的无效操作。
        composeShader_.Gvtcier_Shader_SetFloat("uBloomStrength", 0.22f);
        composeShader_.Gvtcier_Shader_SetFloat("uExposure", exposure);
        // 1.12 是对比度：轻微加对比让画面更通透，1.0 是原样。
        // 超过约 1.2 就开始压暗部细节，所以取 1.12 这个不算激进的值
        composeShader_.Gvtcier_Shader_SetFloat("uContrast", 1.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uSunStrength",
            (fogLevel_ >= 2 && scene.sunEnabled) ? 1.0f : 0.0f);
        // vignette_ 默认 0.7：画面四周压暗的强度，模拟镜头的暗角，
        // 0 表示不做暗角，0.7 是既能聚拢视线又不会明显发黑的中间值
        composeShader_.Gvtcier_Shader_SetFloat("uVignette", vignette_);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, hdrFbo_.colorTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Bloom, Tex2D, bloomMips_[0].colorTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AODepth, Tex2D, gtaoAOTex_ ? gtaoAOTex_ : hdrFbo_.colorTex());
        // 没有体积光时把体积纹理槽也顶到场景颜色上，着色器按强度 0 采样，结果不受影响
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Volume, Tex2D, volTex_[0] ? volTex_[0] : hdrFbo_.colorTex());
        fullscreenQuad_.draw();
        if (fxaaOn_) {
            // FXAA 第二段：读 LDR 缓冲，边缘检测后平滑，直接输出到屏幕
            rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
            rhi::Gvtcier_Rhi_Viewport(0, 0, opt.width, opt.height);
            {
                PassState st;
                st.depthTest = false;
                st.depthWrite = false;
                st.cull = false;
                st.frontFace = Ccw;
                st.blend = false;
                st.scissor = false;
                st.polygonMode = Fill;
                applyPassState(st);
            }
            rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            rhi::Gvtcier_Rhi_Clear(ColorBit);
            fxaaShader_.Gvtcier_Shader_Use();
            fxaaShader_.Gvtcier_Shader_SetInt("uTex", 0);
            fxaaShader_.Gvtcier_Shader_SetVec2("uTexel", 1.0f / (float)rw, 1.0f / (float)rh);
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, ldrFbo_.colorTex());
            fullscreenQuad_.draw();
        }
    } else {
        // 非 HDR 路径：不开 Bloom、不做曝光与对比度调整，只把线性颜色直出到屏幕
        rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
        rhi::Gvtcier_Rhi_Viewport(0, 0, opt.width, opt.height);
        {
            PassState st;
            st.depthTest = false;
            st.depthWrite = false;
            st.cull = false;
            st.frontFace = Ccw;
            st.blend = false;
            st.scissor = false;
            st.polygonMode = Fill;
            applyPassState(st);
        }
        rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        rhi::Gvtcier_Rhi_Clear(ColorBit);
        composeShader_.Gvtcier_Shader_Use();
        composeShader_.Gvtcier_Shader_SetInt("uScene", TexSlot::Scene);
        composeShader_.Gvtcier_Shader_SetInt("uBloom", TexSlot::Bloom);
        composeShader_.Gvtcier_Shader_SetInt("uAO", TexSlot::AODepth);
        composeShader_.Gvtcier_Shader_SetInt("uVolume", TexSlot::Volume);
        composeShader_.Gvtcier_Shader_SetInt("uDepthTex", TexSlot::Depth);
        composeShader_.Gvtcier_Shader_SetFloat("uAOOn", gtaoAOTex_ ? 1.0f : 0.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uBloomStrength", 0.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uExposure", 1.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uExposureBoost", 1.0f);
        // 0.12 是非 HDR 路径的亮度提升阈值：低于它的像素会被轻微提亮，
        // 用来补偿没有 HDR 与曝光时画面偏暗的问题（正常 HDR 路径不需要这个补偿）
        composeShader_.Gvtcier_Shader_SetFloat("uBoostThreshold", 0.12f);
        composeShader_.Gvtcier_Shader_SetFloat("uContrast", 1.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uSunStrength", 0.0f);
        composeShader_.Gvtcier_Shader_SetFloat("uVignette", vignette_);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, hdrFbo_.colorTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Bloom, Tex2D, bloomMips_[0].colorTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::AODepth, Tex2D, gtaoAOTex_ ? gtaoAOTex_ : hdrFbo_.colorTex());
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Volume, Tex2D, volTex_[0] ? volTex_[0] : hdrFbo_.colorTex());
        fullscreenQuad_.draw();
    }

    tickMark(tPost);
    // 后处理完事，最后一段是毛玻璃与 2D 画布
    phase_ = 4;
    if (Gvtcier_StageOver()) return;
    // ---------- pass 24：毛玻璃 ----------
    // 必须在合成之后再画：玻璃要模糊的是“已经调过曝光与色调的最终画面”
    glassPass(opt);
    // ---------- pass 25：2D 画布 ----------
    // UI 永远画在最后，包括游戏 UI 与引擎 UI 两个队列
    Gvtcier_Render_FlushCanvas(opt.width, opt.height);
    tickMark(tCanvas);
    {
        // 性能统计：累计 120 帧后输出各阶段平均耗时与平均帧率，然后清零重新累计
        static int profFrame = 0;
        static double aShadow = 0, aGeom = 0, aGtao = 0, aVol = 0, aPost = 0, aCanvas = 0, aTotal = 0;
        static int profCount = 0;
        aShadow += tShadow; aGeom += tGeom; aGtao += tGtao; aVol += tVol;
        aPost += tPost; aCanvas += tCanvas; aTotal += Gvtcier_NowSeconds() - tFrame0;
        ++profCount;
        if (++profFrame >= 120) {
            profFrame = 0;
            if (profCount > 0) {
                double n = (double)profCount;
                printf("[prof] avg ms  total=%.2f shadow=%.2f geom=%.2f gtao=%.2f vol=%.2f post=%.2f canvas=%.2f fps=%.1f\n",
                      aTotal / n * 1000.0, aShadow / n * 1000.0, aGeom / n * 1000.0, aGtao / n * 1000.0,
                      aVol / n * 1000.0, aPost / n * 1000.0, aCanvas / n * 1000.0,
                      aTotal > 0.0 ? n / aTotal : 0.0);
                fflush(stdout);
            }
            aShadow = aGeom = aGtao = aVol = aPost = aCanvas = aTotal = 0.0;
            profCount = 0;
        }
    }
}

// 接收外部（物理/调试系统）送来的线框数据，打包成顶点。
// data 是扁平 float 数组，每个顶点 7 个 float：位置 xyz + 颜色 rgba。
// 为了省掉每帧无谓的上传，先把新数据和缓存逐字比对，完全一致就直接返回。
void Renderer::Gvtcier_Render_SetDebugLines(const float* data, int floatCount) {
    const int kStride = 7;
    int n = (data && floatCount > 0) ? floatCount / kStride : 0;
    bool same = ((int)dbgLines_.size() == n) && dbgLinesDirty_;
    if (same) {
        for (int i = 0; i < n; ++i) {
            const float* p = data + (size_t)i * kStride;
            const DebugLineVertex& v = dbgLines_[(size_t)i];
            if (v.pos[0] != p[0] || v.pos[1] != p[1] || v.pos[2] != p[2] ||
                v.r != p[3] || v.g != p[4] || v.b != p[5] || v.a != p[6]) {
                same = false;
                break;
            }
        }
    }
    if (same) return;
    dbgLines_.clear();
    dbgLinesDirty_ = false;
    if (!data || floatCount <= 0) return;
    dbgLines_.resize((size_t)n);
    for (int i = 0; i < n; ++i) {
        const float* p = data + (size_t)i * kStride;
        DebugLineVertex& v = dbgLines_[(size_t)i];
        v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = p[2];
        v.r = p[3]; v.g = p[4]; v.b = p[5]; v.a = p[6];
    }
    dbgLinesDirty_ = true;
}

// 清空世界线缓存。通常在一帧开始、或场景切换时调用。
void Renderer::Gvtcier_Render_ClearWorldLines() {
    worldLines_.clear();
}

// 追加一条世界空间线段（两端点坐标 + 颜色）。这类线只在本帧绘制，画完即清空。
// 65536 是顶点数上限（约 3.2 万条线），超过就丢弃，避免脚本刷线把内存撑爆。
void Renderer::Gvtcier_Render_AddWorldLine(float x1, float y1, float z1,
                                           float x2, float y2, float z2,
                                           float r, float g, float b, float a) {
    if (worldLines_.size() + 2 > 65536) return;
    DebugLineVertex v1;
    v1.pos[0] = x1; v1.pos[1] = y1; v1.pos[2] = z1;
    v1.r = r; v1.g = g; v1.b = b; v1.a = a;
    DebugLineVertex v2;
    v2.pos[0] = x2; v2.pos[1] = y2; v2.pos[2] = z2;
    v2.r = r; v2.g = g; v2.b = b; v2.a = a;
    worldLines_.push_back(v1);
    worldLines_.push_back(v2);
}

// 用 gizmo 着色器画一批调试线段。
// 线框不做深度测试（总画在最前面）、开 alpha 混合，这样被墙挡住的调试信息也看得见。
// dbgLinesDirty_ 为 true 时才上传顶点，避免同一批线重复上传。
void Renderer::drawDebugLines(const Mat4& vp, const std::vector<DebugLineVertex>& verts) {
    if (verts.empty() || !dbgLineVao_ || !gizmoShader_.Gvtcier_Shader_Id()) return;

    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = true;
        st.blendSrc = SrcAlpha;
        st.blendDst = OneMinusSrcAlpha;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }

    gizmoShader_.Gvtcier_Shader_Use();
    gizmoShader_.Gvtcier_Shader_SetMat4("uVP", vp.m);
    rhi::Gvtcier_Rhi_BindVertexArray(dbgLineVao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, dbgLineVbo_);
    if (dbgLinesDirty_) {
        rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, verts.size() * sizeof(DebugLineVertex),
                                    verts.data(), DynamicDraw);
        dbgLinesDirty_ = false;
    }
    rhi::Gvtcier_Rhi_DrawArrays(Lines, 0, (int)verts.size());
    rhi::Gvtcier_Rhi_BindVertexArray(0);

    {
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
}

// 注册用户自定义的 GL 绘制回调。引擎会在每帧的 UserGlFrame 阶段调用它，
// 让游戏/插件在引擎画完之后直接往默认帧缓冲上画东西。
void Renderer::Gvtcier_Render_SetUserGlDraw(Gvtcier_UserGlDrawFn fn, void* user) {
    userGlDraw_ = fn;
    userGlUser_ = user;
}

// 执行用户自定义 GL 回调。会先把渲染目标切成默认帧缓冲并把视口设成窗口尺寸，
// 保证用户的代码不用自己处理局面；传 0 的宽高回退到默认窗口尺寸。
// 回调结束后解绑 VAO 与着色器程序，回到干净状态。
void Renderer::Gvtcier_Render_UserGlFrame(int width, int height) {
    rhi::Gvtcier_Rhi_BindFramebuffer(rhi::Framebuffer, 0);
    rhi::Gvtcier_Rhi_Viewport(0, 0, width > 0 ? width : gv::kDefaultWindowWidth, height > 0 ? height : gv::kDefaultWindowHeight);
    if (userGlDraw_) userGlDraw_(userGlUser_);
    rhi::Gvtcier_Rhi_BindVertexArray(0);
    rhi::Gvtcier_Rhi_UseProgram(0);
}

// 只跑 2D 绘制：先画游戏画布，再画引擎画布（引擎 UI 盖在上面）。
// 用于纯 2D 场景或编辑器界面，不涉及任何 3D 资源。
void Renderer::Gvtcier_Render_Render2D(int w, int h) {
    viewW_ = w > 0 ? w : gv::kDefaultWindowWidth;
    viewH_ = h > 0 ? h : gv::kDefaultWindowHeight;
    renderCanvasItems2D();
    if (!engineItems_.empty()) {
        flushQueue(engineItems_, viewW_, viewH_);
        engineItems_.clear();
    }
}

// 帧末统一把 2D 画布队列刷出去。
// 2D 模式下走 renderCanvasItems2D；3D 模式下按 游戏 UI -> 引擎 UI 的顺序分别提交，
// 保证引擎的调试面板永远在游戏 UI 之上。
void Renderer::Gvtcier_Render_FlushCanvas(int w, int h) {
    if (mode2D_) {
        Gvtcier_Render_Render2D(w, h);
        canvasItems_.clear();
        engineItems_.clear();
        return;
    }
    flushQueue(canvasItems_, w, h);
    flushQueue(engineItems_, w, h);
}

// 批量提交一个 2D 图元队列，是 3D 路径下的 2D 绘制实现（renderCanvasItems2D 的精简版没有的功能：
// 支持图片图元、支持文字顶点缓存）。
//
// 处理方式与 renderCanvasItems2D 类似，但有两点不同：
//   1. 多了 Image 类型，按 纹理 + 透明度 分组提交，换纹理才 flush
//   2. 文字按 “文本内容 + 缩放” 缓存字形四边形（textCache_，最多 128 条，超了删最老 32 条），
//      之后每帧只需把缓存的局部坐标平移并换算到 NDC，省掉逐字重复计算的代价
// 顺序遍历时会按类型切换 flush，保证不同图元之间的层叠顺序正确。
// 结束后清空 items 并恢复主几何状态。
void Renderer::flushQueue(std::vector<CanvasPrim>& items, int w, int h) {
    if (items.empty()) return;
    viewW_ = w;
    viewH_ = h;
    {
        PassState st;
        st.depthTest = false;
        st.depthWrite = false;
        st.cull = false;
        st.frontFace = Ccw;
        st.blend = true;
        st.blendSrc = SrcAlpha;
        st.blendDst = OneMinusSrcAlpha;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const CanvasPrim& a, const CanvasPrim& b) { return a.z < b.z; });
    // 三种图元各自的顶点缓冲：矩形 6 float/顶点、文字 4 float/顶点、图片 4 float/顶点
    std::vector<float> rverts;
    std::vector<float> tverts;
    std::vector<float> iverts;
    // curImageTex/curImageAlpha 记录当前图片批用的纹理与透明度，变化时才拆批
    bool rectBound = false, textBound = false, imageBound = false;
    unsigned int curImageTex = 0;
    float curImageAlpha = 1.0f;
    auto flushRect = [&]() {
        if (rverts.empty()) return;
        if (!rectBound) {
            rect2dShader_.Gvtcier_Shader_Use();
            rhi::Gvtcier_Rhi_BindVertexArray(rect2dVao_);
            rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, rect2dVbo_);
            rectBound = true;
        }
        rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(rverts.size() * sizeof(float)), rverts.data(), DynamicDraw);
        rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)(rverts.size() / 6));
        rverts.clear();
    };
    auto flushText = [&](float cr, float cg, float cb, float ca, int page) {
        if (tverts.empty()) return;
        if (!textBound) {
            textShader_.Gvtcier_Shader_Use();
            textShader_.Gvtcier_Shader_SetInt("uFont", 0);
            rhi::Gvtcier_Rhi_BindVertexArray(textVao_);
            rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, textVbo_);
            textBound = true;
        }
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, font_.textureAt(page));
        textShader_.Gvtcier_Shader_Use();
        textShader_.Gvtcier_Shader_SetVec4("uColor", cr, cg, cb, ca);
        rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(tverts.size() * sizeof(float)), tverts.data(), DynamicDraw);
        rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)(tverts.size() / 4));
        tverts.clear();
    };
    auto flushImage = [&]() {
        if (iverts.empty()) return;
        if (!imageBound) {
            // 图片和文字共用同一套 VAO（都是 屏幕坐标 xy + uv），只是着色器不同
            imageShader_.Gvtcier_Shader_Use();
            imageShader_.Gvtcier_Shader_SetInt("uTex", 0);
            rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, curImageTex);
            rhi::Gvtcier_Rhi_BindVertexArray(textVao_);
            rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, textVbo_);
            imageBound = true;
        }
        imageShader_.Gvtcier_Shader_SetVec4("uTint", 1.0f, 1.0f, 1.0f, curImageAlpha);
        rhi::Gvtcier_Rhi_BindTextureUnit(TexSlot::Scene, Tex2D, curImageTex);
        rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(iverts.size() * sizeof(float)), iverts.data(), DynamicDraw);
        rhi::Gvtcier_Rhi_DrawArrays(Triangles, 0, (int)(iverts.size() / 4));
        iverts.clear();
    };
    auto pushGlyph = [&](const FontGlyph* g, float penX, float baseY, float sc) {
        if (!g || g->w <= 0 || g->h <= 0) return;
        float x0 = penX + g->ox * sc;
        float y0 = baseY + (font_.ascent() - g->oy) * sc;
        float x1 = x0 + g->w * sc;
        float y1 = y0 + g->h * sc;
        float px0 = x0 / viewW_ * 2.0f - 1.0f;
        float py0 = 1.0f - y0 / viewH_ * 2.0f;
        float px1 = x1 / viewW_ * 2.0f - 1.0f;
        float py1 = 1.0f - y1 / viewH_ * 2.0f;
        float v[24] = {
            px0, py0, g->u0, g->v0,
            px1, py0, g->u1, g->v0,
            px0, py1, g->u0, g->v1,
            px1, py0, g->u1, g->v0,
            px1, py1, g->u1, g->v1,
            px0, py1, g->u0, g->v1,
        };
        for (int i = 0; i < 24; ++i) tverts.push_back(v[i]);
    };
    for (const CanvasPrim& p : items) {
        if (p.type == CanvasPrim::Rect) {
            // 换图元类型时先把前面攒的批全部 flush 掉，保证绘制顺序
            flushText(1.0f, 1.0f, 1.0f, 1.0f, 0);
            textBound = false;
            flushImage();
            imageBound = false;
            float x0 = p.x, y0 = p.y, x1 = p.x + p.w, y1 = p.y + p.h;
            float px0 = x0 / viewW_ * 2.0f - 1.0f;
            float py0 = 1.0f - y0 / viewH_ * 2.0f;
            float px1 = x1 / viewW_ * 2.0f - 1.0f;
            float py1 = 1.0f - y1 / viewH_ * 2.0f;
            float v[36] = {
                px0, py0, p.r, p.g, p.b, p.a,  px1, py0, p.r, p.g, p.b, p.a,
                px1, py1, p.r, p.g, p.b, p.a,  px0, py0, p.r, p.g, p.b, p.a,
                px1, py1, p.r, p.g, p.b, p.a,  px0, py1, p.r, p.g, p.b, p.a,
            };
            for (int i = 0; i < 36; ++i) rverts.push_back(v[i]);
        } else if (p.type == CanvasPrim::Image) {
            flushRect();
            rectBound = false;
            flushText(1.0f, 1.0f, 1.0f, 1.0f, 0);
            textBound = false;
            if (curImageTex != p.tex || curImageAlpha != p.a) {
                flushImage();
            }
            curImageTex = p.tex;
            curImageAlpha = p.a;
            float x0 = p.x, y0 = p.y, x1 = p.x + p.w, y1 = p.y + p.h;
            float px0 = x0 / viewW_ * 2.0f - 1.0f;
            float py0 = 1.0f - y0 / viewH_ * 2.0f;
            float px1 = x1 / viewW_ * 2.0f - 1.0f;
            float py1 = 1.0f - y1 / viewH_ * 2.0f;
            float v[24] = {
                px0, py0, 0.0f, 1.0f,
                px1, py0, 1.0f, 1.0f,
                px0, py1, 0.0f, 0.0f,
                px1, py0, 1.0f, 1.0f,
                px1, py1, 1.0f, 0.0f,
                px0, py1, 0.0f, 0.0f,
            };
            for (int i = 0; i < 24; ++i) iverts.push_back(v[i]);
        } else {
            flushRect();
            rectBound = false;
            flushImage();
            imageBound = false;
            TextCacheEntry* ce = nullptr;
            // 先查文字缓存：键是 文本 + 缩放。命中就只剩平移换算，不用再逐字取字形
            for (TextCacheEntry& e : textCache_) {
                if (e.scale == p.scale && e.text == p.text) { ce = &e; break; }
            }
            if (!ce) {
                // 缓存上限 128 条，超了删掉最老的 32 条（一次性删，避免每帧都挪整张表）
                if (textCache_.size() >= 128) {
                    textCache_.erase(textCache_.begin(), textCache_.begin() + 32);
                }
                textCache_.push_back(TextCacheEntry());
                ce = &textCache_.back();
                ce->text = p.text;
                ce->scale = p.scale;
                const float px = p.scale * (32.0f / (float)FontAtlas::kBasePx);
                float pen = 0.0f;
                // 缓存的是“以文字左上角为原点”的局部坐标，每帧再平移到实际位置。
                // 字形按所在图集页分组，同一个顶点数组里记下每段属于哪一页，绘制时按页切换纹理。
                const char* s = p.text.c_str();
                while (*s) {
                    unsigned int cp = gvUtf8Next(s);
                    const FontGlyph* g = font_.glyph(cp);
                    if (!g) continue;
                    float bx0 = pen + g->ox * px;
                    float by0 = (float)(font_.ascent() - g->oy) * px;
                    float bx1 = bx0 + g->w * px;
                    float by1 = by0 + g->h * px;
                    float v[24] = {
                        bx0, by0, g->u0, g->v0,
                        bx1, by0, g->u1, g->v0,
                        bx0, by1, g->u0, g->v1,
                        bx1, by0, g->u1, g->v0,
                        bx1, by1, g->u1, g->v1,
                        bx0, by1, g->u0, g->v1,
                    };
                    for (int i = 0; i < 24; ++i) ce->body.push_back(v[i]);
                    ce->pages.push_back(g->page);
                    pen += g->advance * px;
                }
            }
            auto pushCached = [&](const std::vector<float>& src, float ox, float oy, size_t from, size_t to) {
                // 把缓存的局部坐标（每 24 个 float 一个四边形）平移到 (ox,oy) 并转成 NDC
                for (size_t i = from * 24; i + 24 <= to * 24 && i + 24 <= src.size(); i += 24) {
                    float v[24];
                    for (int k = 0; k < 6; ++k) {
                        float lx = ox + src[i + (size_t)k * 4 + 0];
                        float ly = oy + src[i + (size_t)k * 4 + 1];
                        v[k * 4 + 0] = lx / viewW_ * 2.0f - 1.0f;
                        v[k * 4 + 1] = 1.0f - ly / viewH_ * 2.0f;
                        v[k * 4 + 2] = src[i + (size_t)k * 4 + 2];
                        v[k * 4 + 3] = src[i + (size_t)k * 4 + 3];
                    }
                    for (int k = 0; k < 24; ++k) tverts.push_back(v[k]);
                }
            };
            // 按图集页分段提交：pages 里每两个数是一段（页号, 起始四边形下标），
            // 段与段之间只切纹理，颜色始终用图元自身的
            size_t quadCount = ce->body.size() / 24;
            size_t runStart = 0;
            while (runStart < quadCount) {
                int page = 0;
                if (runStart * 2 < ce->pages.size()) page = ce->pages[runStart * 2];
                size_t runEnd = runStart + 1;
                while (runEnd < quadCount) {
                    int nextPage = 0;
                    if (runEnd * 2 < ce->pages.size()) nextPage = ce->pages[runEnd * 2];
                    if (nextPage != page) break;
                    ++runEnd;
                }
                pushCached(ce->body, p.x, p.y, runStart, runEnd);
                flushText(p.r, p.g, p.b, p.a, page);
                runStart = runEnd;
            }
        }
    }
    flushRect();
    flushImage();
    rhi::Gvtcier_Rhi_BindVertexArray(0);
    {
        PassState st;
        st.depthTest = true;
        st.depthWrite = true;
        st.depthFunc = Greater;
        st.cull = true;
        st.cullMode = Back;
        st.frontFace = Ccw;
        st.blend = false;
        st.scissor = false;
        st.polygonMode = Fill;
        applyPassState(st);
    }
    items.clear();
}

// “无信号”占位画面：整屏黑底 + 居中的一行提示文字，用于相机失联等异常情况。
// 字宽逐字累加算出，再按屏幕中线水平居中；纵向固定居中偏上 24 像素（视觉上更居中）。
void Renderer::Gvtcier_Render_DrawNoSignal(int w, int h, const char* utf8) {
    rhi::Gvtcier_Rhi_Viewport(0, 0, w, h);
    rhi::Gvtcier_Rhi_ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    rhi::Gvtcier_Rhi_Clear(ColorBit);
    float scale = 2.2f;
    float textW = 0.0f;
    for (const char* s = utf8; *s;) {
        unsigned int cp = gvUtf8Next(s);
        const FontGlyph* g = font_.glyph(cp);
        if (g) textW += g->advance * scale;
    }
    Gvtcier_Render_DrawText((int)((w - textW) / 2.0f), (int)(h / 2 - 24), scale, utf8);
    Gvtcier_Render_FlushCanvas(w, h);
}

// 暴露特效系统给外部，用于直接创建/控制特效实例。
EffectSystem& Renderer::Gvtcier_Render_EffectSystem() { return effectSystem_; }

// 按路径取特效资源 ID；已经加载过的直接返回缓存里的，没加载过就现加载。
GvtId<EffectAsset> Renderer::Gvtcier_Render_EffectAssetFor(const std::string& path) {
    return effectAssetFor(path);
}

// effectAssetFor 的实现：先查缓存表（线性查找，特效资源数量通常很少），
// 未命中则从 JSON 文件加载，注册到 ID 系统后记进缓存表。
// 返回无效 ID 表示加载失败（文件不存在或格式错误），调用方需要判 valid()。
GvtId<EffectAsset> Renderer::effectAssetFor(const std::string& path) {
    for (auto& kv : effectAssetIds_) {
        if (kv.first == path) return kv.second;
    }
    EffectAsset a;
    if (!a.Gvtcier_Effect_LoadJSON(path.c_str())) return GvtId<EffectAsset>{};
    GvtId<EffectAsset> id = effectAssets_.Gvtcier_Object_Register(std::move(a));
    effectAssetIds_.push_back({ path, id });
    return id;
}

// 让特效实例的播放状态跟随场景里的 Effect 对象。
// 每个 Effect 对象在 effectPlaying_ 里记一个状态：
//   0 = 未播放，1 = 待播放（占位，当前流程不会停留在这个状态），2 = 正在播放
//
// 三个阶段：
//   1. 清场：已有实例绑定的对象被删了、或者类型/资源路径变了，就停掉那个实例
//   2. 逐对象对账：Effect 对象且自动播放时确保有实例在播（必要时按资源路径加载资产并 Play）；
//      非 Effect 或不可见时停掉实例；已经在播的就跟着对象位置走
//   3. 清理：把已经不存在的对象从状态表里删掉，防止表随对象 ID 无限增长
//
// 激光类型（effectType==1）和没有资源路径的对象不在这里处理，它们走另一套逻辑。
void Renderer::Gvtcier_Render_SyncSceneEffects(Scene& scene) {
    std::vector<GvtId<Object>> stale;
    effectSystem_.instances().ForEachAlive([&](const EffectInstance& inst, GvtId<EffectInstance>) {
        if (inst.boundAssetPath.empty()) return;
        if (!inst.boundObject.valid()) return;
        const Object* bo = scene.Gvtcier_Scene_GetObject(inst.boundObject);
        bool dead = !bo || bo->kind != ObjKind::Effect || bo->effectAsset != inst.boundAssetPath;
        if (dead) stale.push_back(inst.boundObject);
    });
    for (GvtId<Object> bi : stale) {
        GvtId<EffectInstance> gid = effectSystem_.Gvtcier_Effect_FindByObject(bi);
        EffectInstance* p = effectSystem_.instances().Gvtcier_Object_Get(gid);
        if (p) p->Gvtcier_Effect_Stop();
    }
    std::map<uint64_t, int> seen;
    // 本轮扫到的对象 ID 都记进 seen，末尾据此清理掉已消失对象的旧状态
    scene.ForEachObject([&](Object& o, GvtId<Object> id) {
        uint64_t key = ((uint64_t)id.index << 32) | (uint64_t)id.generation;
        int state = 0;
        std::map<uint64_t, int>::iterator old = effectPlaying_.find(key);
        if (old != effectPlaying_.end()) state = old->second;
        if (o.kind != ObjKind::Effect) { effectPlaying_[key] = 0; seen[key] = 1; return; }
        if (o.effectType == 1 || o.effectAsset.empty()) { effectPlaying_[key] = 0; seen[key] = 1; return; }
        if (!o.visible || !o.autoPlay) {
            if (state == 2) {
                GvtId<EffectInstance> gid = effectSystem_.Gvtcier_Effect_FindByObject(id);
                EffectInstance* p = effectSystem_.instances().Gvtcier_Object_Get(gid);
                if (p) p->Gvtcier_Effect_Stop();
            }
            effectPlaying_[key] = 0;
            seen[key] = 1;
            return;
        }
        if (state == 2) {
            // 已在播放：只同步位置，不重启特效，避免每帧重置动画
            GvtId<EffectInstance> gid = effectSystem_.Gvtcier_Effect_FindByObject(id);
            EffectInstance* p = effectSystem_.instances().Gvtcier_Object_Get(gid);
            if (p && p->playing) p->position = o.pos;
            seen[key] = 1;
            return;
        }
        GvtId<EffectAsset> asset = effectAssetFor(o.effectAsset);
        if (!asset.valid()) { effectPlaying_[key] = 0; seen[key] = 1; return; }
        EffectSystem& es = effectSystem_;
        GvtId<EffectInstance> oldId = es.Gvtcier_Effect_FindByObject(id);
        if (!oldId.valid()) {
            GvtId<EffectInstance> h = es.Gvtcier_Effect_Play(asset, o.pos, o.loop, id);
            EffectInstance* p = es.instances().Gvtcier_Object_Get(h);
            if (p) p->boundAssetPath = o.effectAsset;
        }
        effectPlaying_[key] = 2;
        seen[key] = 1;
    });
    for (std::map<uint64_t, int>::iterator it = effectPlaying_.begin(); it != effectPlaying_.end();) {
        if (seen.find(it->first) == seen.end()) it = effectPlaying_.erase(it);
        else ++it;
    }
}

