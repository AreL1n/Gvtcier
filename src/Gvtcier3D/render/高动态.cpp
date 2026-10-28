#include "Gvtcier.h"
#include <cstdio>

using namespace rhi;

// 创建 HDR 主帧缓冲，供延迟渲染使用。
// 一共四张全分辨率颜色附件，对应延迟着色不同阶段的需要：
//   attachment0 colorTex  Rgba32f：最终 HDR 颜色（几何 pass 先写 albedo*光照，光照 pass 再覆盖；
//                          延迟光照 pass 的 fullscreenQuad 也往这写，所以要浮点精度扛 HDR 亮度）
//   attachment1 normalTex Rgba16F：世界空间法线 + 深度辅助信息，供 GTAO 等使用
//   attachment2 albedoTex Rgba16F：albedo 与粗糙度等材质信息
//   attachment3 materialTex Rgba16F：材质附加参数（例如高光、金属度）
// 只有 colorTex 用 Linear（后处理要放大缩小采样），G/N/A 类附件用 Nearest（逐像素精确读取）。
// withDepth 为 true 时额外挂一张 DepthComponent32F 深度纹理，采用反向 Z（清 0 / Greater）。
// 返回 false 表示帧缓冲不完整，调用方需要处理（通常意味着分辨率或格式不被硬件支持）。
bool ColorFbo::create(int width, int height, bool withDepth) {
    destroy();
    if (width <= 0 || height <= 0) return false;
    width_ = width;
    height_ = height;

    rhi::Gvtcier_Rhi_GenTextures(1, &colorTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, colorTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba32f, width, height, Rgba, Float, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);

    rhi::Gvtcier_Rhi_GenTextures(1, &normalTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, normalTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba16F, width, height, Rgba, Float, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);

    rhi::Gvtcier_Rhi_GenTextures(1, &albedoTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, albedoTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba16F, width, height, Rgba, Float, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);

    rhi::Gvtcier_Rhi_GenTextures(1, &materialTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, materialTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba16F, width, height, Rgba, Float, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);

    rhi::Gvtcier_Rhi_GenFramebuffers(1, &fbo_);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment0, Tex2D, colorTex_, 0);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment1, Tex2D, normalTex_, 0);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment2, Tex2D, albedoTex_, 0);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment3, Tex2D, materialTex_, 0);
    // 必须显式声明四路颜色输出，着色器里才会真正写进 G-Buffer
    {
        const unsigned bufs[4] = { ColorAttachment0, ColorAttachment1, ColorAttachment2, ColorAttachment3 };
        rhi::Gvtcier_Rhi_DrawBuffers(4, bufs);
    }
    if (withDepth) {
        rhi::Gvtcier_Rhi_GenTextures(1, &depthTex_);
        rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, depthTex_);
        rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, DepthComponent32F, width, height, DepthComponent, Float, nullptr);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
        rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, DepthAttachment, Tex2D, depthTex_, 0);
    }
    unsigned status = rhi::Gvtcier_Rhi_CheckFramebufferStatus(Framebuffer);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    if (status != FramebufferComplete) {
        printf("[hdr] framebuffer incomplete: 0x%X\n", status);
        return false;
    }
    printf("[hdr] color framebuffer %dx%d ready\n", width, height);
    return true;
}

// 创建只用一张 Rgba8 颜色的离屏缓冲，用于模糊链（Bloom 各级、玻璃背景模糊）。
// 用 8 位就够：这些图只做视觉模糊，不做光照运算；空间省一半，带宽也低很多。
bool ColorFbo::createBlur(int width, int height) {
    destroy();
    if (width <= 0 || height <= 0) return false;
    width_ = width;
    height_ = height;
    rhi::Gvtcier_Rhi_GenTextures(1, &colorTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, colorTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Rgba8, width, height, Rgba, UnsignedByte, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
    rhi::Gvtcier_Rhi_GenFramebuffers(1, &fbo_);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment0, Tex2D, colorTex_, 0);
    rhi::Gvtcier_Rhi_DrawBuffer(ColorAttachment0);
    unsigned status = rhi::Gvtcier_Rhi_CheckFramebufferStatus(Framebuffer);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    if (status != FramebufferComplete) {
        printf("[hdr] blur framebuffer incomplete: 0x%X\n", status);
        return false;
    }
    return true;
}

// 创建单通道（R8，无符号字节）的离屏缓冲，专给 GTAO 用。
// attachments 只允许 1 或 2：
//   1 —— 只有 colorTex（AO 结果图）
//   2 —— 左边再多一张 normalTex（AO 边缘信息），供去噪 pass 做边缘感知模糊
// colorTex 用 Linear（上采样到全分辨率时不会出块状），第二张用 Nearest。
// 返回 false 表示参数非法或帧缓冲不完整。
bool ColorFbo::createR8(int width, int height, int attachments) {
    destroy();
    if (width <= 0 || height <= 0 || attachments < 1 || attachments > 2) return false;
    width_ = width;
    height_ = height;
    rhi::Gvtcier_Rhi_GenTextures(1, &colorTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, colorTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, R8, width, height, Red, UnsignedByte, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
    rhi::Gvtcier_Rhi_GenFramebuffers(1, &fbo_);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment0, Tex2D, colorTex_, 0);
    if (attachments == 2) {
        rhi::Gvtcier_Rhi_GenTextures(1, &normalTex_);
        rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, normalTex_);
        rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, R8, width, height, Red, UnsignedByte, nullptr);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);
        rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment1, Tex2D, normalTex_, 0);
        const unsigned bufs[2] = { ColorAttachment0, ColorAttachment1 };
        rhi::Gvtcier_Rhi_DrawBuffers(2, bufs);
    } else {
        rhi::Gvtcier_Rhi_DrawBuffer(ColorAttachment0);
    }
    unsigned status = rhi::Gvtcier_Rhi_CheckFramebufferStatus(Framebuffer);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    if (status != FramebufferComplete) {
        printf("[hdr] r8 framebuffer incomplete: 0x%X\n", status);
        return false;
    }
    return true;
}

// 释放这个离屏缓冲持有的全部 GL 对象（渲染缓冲、各附件纹理、帧缓冲），
// 句柄和尺寸一起清零，重复调用安全。
void ColorFbo::destroy() {
    if (depthRbo_) rhi::Gvtcier_Rhi_DeleteRenderbuffers(1, &depthRbo_);
    if (depthTex_) rhi::Gvtcier_Rhi_DeleteTextures(1, &depthTex_);
    if (fbo_) rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &fbo_);
    if (colorTex_) rhi::Gvtcier_Rhi_DeleteTextures(1, &colorTex_);
    if (normalTex_) rhi::Gvtcier_Rhi_DeleteTextures(1, &normalTex_);
    if (albedoTex_) rhi::Gvtcier_Rhi_DeleteTextures(1, &albedoTex_);
    if (materialTex_) rhi::Gvtcier_Rhi_DeleteTextures(1, &materialTex_);
    depthRbo_ = 0;
    depthTex_ = 0;
    fbo_ = 0;
    colorTex_ = 0;
    normalTex_ = 0;
    albedoTex_ = 0;
    materialTex_ = 0;
    width_ = 0;
    height_ = 0;
}

// 把帧缓冲设为当前渲染目标，并把视口设成它的实际尺寸（不是屏幕尺寸）。
void ColorFbo::bind() {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_Viewport(0, 0, width_, height_);
}

// 解除绑定，回到默认帧缓冲，并把视口还原成屏幕尺寸。
void ColorFbo::unbind(int screenW, int screenH) {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    rhi::Gvtcier_Rhi_Viewport(0, 0, screenW, screenH);
}

// 切换颜色附件的过滤方式：linear 为 true 用双线性（需要缩放采样时用），
// 否则用最近点（需要逐像素精确对齐时用，比如 FXAA 输入）。
void ColorFbo::setColorFilter(bool linear) {
    if (!colorTex_) return;
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, colorTex_);
    if (linear) {
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Linear);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Linear);
    } else {
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
        rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
    }
}
