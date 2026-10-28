#include "Gvtcier.h"

using namespace rhi;

// 创建单张 2D 深度阴影图（体积光的正交投影用这一套）。
// 这一整段流程说明本工程的深度约定：
//   - 深度纹理只做深度附件，不挂颜色附件（DrawBuffer/ReadBuffer 都设成 None）
//   - 清深度值统一用 0.0，深度测试用 Greater（反向 Z），所以“越近越大”
//   - 采样用 Nearest，避免 PC F 插值把阴影边缘糊掉；夹取到边缘防止越界采样
// 参数 size 是正方形边长像素；返回 false 表示帧缓冲不完整或尺寸非法。
bool ShadowMap::create(int size) {
    destroy();
    if (size <= 0) return false;
    size_ = size;
    layered_ = false;
    layers_ = 1;

    rhi::Gvtcier_Rhi_GenTextures(1, &depthTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2D, depthTex_);
    rhi::Gvtcier_Rhi_TexImage2D(Tex2D, 0, Depth24, size, size,
                    DepthComponent, UnsignedInt, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMinFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexMagFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2D, TexWrapT, ClampToEdge);

    // 只挂深度附件，不挂颜色，因此必须显式关掉 Draw/ReadBuffer，
    // 否则某些驱动会因为“没有颜色附件”判帧缓冲不完整
    rhi::Gvtcier_Rhi_GenFramebuffers(1, &fbo_);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, DepthAttachment, Tex2D, depthTex_, 0);
    rhi::Gvtcier_Rhi_DrawBuffer(None);
    rhi::Gvtcier_Rhi_ReadBuffer(None);
    unsigned status = rhi::Gvtcier_Rhi_CheckFramebufferStatus(Framebuffer);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);

    if (status != FramebufferComplete) {
        printf("[shadow] framebuffer incomplete: 0x%X\n", status);
        return false;
    }
    printf("[shadow] shadow map %dx%d ready\n", size, size);
    return true;
}

// 创建级联阴影用的深度纹理数组，一层对应一个 CSM 级联。
// 与单张版本的区别：
//   - 纹理目标是 Tex2DArray，格式用 32 位浮点深度（DepthComponent32F），
//     级联切分后的距离跨度很大，24 位深度在远处会有明显精度问题
//   - 帧缓冲先只绑定第 0 层做完整性检查，渲染时由 bindLayer 逐层改绑
// size 是每层边长，layers 是级联层数（太阳阴影固定 4 层）。
bool ShadowMap::createLayered(int size, int layers) {
    destroy();
    if (size <= 0 || layers <= 0) return false;
    size_ = size;
    layered_ = true;
    layers_ = layers;

    rhi::Gvtcier_Rhi_GenTextures(1, &depthTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, Tex2DArray, depthTex_);
    rhi::Gvtcier_Rhi_TexImage3D(Tex2DArray, 0, DepthComponent32F, size, size, layers,
                    DepthComponent, Float, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexMinFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexMagFilter, Nearest);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(Tex2DArray, TexWrapT, ClampToEdge);

    rhi::Gvtcier_Rhi_GenFramebuffers(1, &fbo_);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_FramebufferTextureLayer(Framebuffer, DepthAttachment, depthTex_, 0, 0);
    rhi::Gvtcier_Rhi_DrawBuffer(None);
    rhi::Gvtcier_Rhi_ReadBuffer(None);
    unsigned status = rhi::Gvtcier_Rhi_CheckFramebufferStatus(Framebuffer);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);

    if (status != FramebufferComplete) {
        printf("[shadow] layered framebuffer incomplete: 0x%X\n", status);
        return false;
    }
    printf("[shadow] shadow map array %dx%d x%d ready\n", size, size, layers);
    return true;
}

// 释放帧缓冲与深度纹理，尺寸清零。layered_ / layers_ 不清，用于标识“曾经是什么类型”，
// 下次 create 会重写。
void ShadowMap::destroy() {
    if (fbo_) { rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &fbo_); fbo_ = 0; }
    if (depthTex_) { rhi::Gvtcier_Rhi_DeleteTextures(1, &depthTex_); depthTex_ = 0; }
    size_ = 0;
}

// 绑定阴影帧缓冲、把视口切成阴影图尺寸，并清深度为 0（反向 Z 下的“最远”）。
// 之后所有绘制都会写进阴影图，绘制完要调用 unbind 回到屏幕。
void ShadowMap::bind() {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_Viewport(0, 0, size_, size_);
    rhi::Gvtcier_Rhi_ClearDepth(0.0);
    rhi::Gvtcier_Rhi_Clear(DepthBit);
}

// 绑定数组阴影图的第 layer 层并清深度。
// 记录 boundLayer_，unbind 时据此把帧缓冲恢复成第 0 层，避免下一帧误写上层。
void ShadowMap::bindLayer(int layer) {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    if (layered_) rhi::Gvtcier_Rhi_FramebufferTextureLayer(Framebuffer, DepthAttachment, depthTex_, 0, layer);
    rhi::Gvtcier_Rhi_Viewport(0, 0, size_, size_);
    rhi::Gvtcier_Rhi_ClearDepth(0.0);
    rhi::Gvtcier_Rhi_Clear(DepthBit);
    boundLayer_ = layer;
    boundDepthOnly_ = false;
}

// 和 bindLayer 做完全一样的事，只是把 boundDepthOnly_ 记成 true，
// 供 unbind 判断“当前处于仅深度模式”，需要额外恢复 Draw/ReadBuffer 为 None。
void ShadowMap::bindLayerDepthOnly(int layer) {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    if (layered_) rhi::Gvtcier_Rhi_FramebufferTextureLayer(Framebuffer, DepthAttachment, depthTex_, 0, layer);
    rhi::Gvtcier_Rhi_Viewport(0, 0, size_, size_);
    rhi::Gvtcier_Rhi_ClearDepth(0.0);
    rhi::Gvtcier_Rhi_Clear(DepthBit);
    boundLayer_ = layer;
    boundDepthOnly_ = true;
}

// 解除阴影图绑定：恢复默认帧缓冲（0 号 FBO），视口改回屏幕尺寸。
// 如果之前绑的是非 0 层（或仅深度模式），顺手把附件重置回第 0 层并关掉颜色输出，
// 保证这个帧缓冲随时都处于“可写且合法”的状态。
// screenW/screenH 是主渲染目标尺寸。
void ShadowMap::unbind(int screenW, int screenH) {
    if (layered_ && (boundLayer_ != 0 || boundDepthOnly_)) {
        rhi::Gvtcier_Rhi_FramebufferTextureLayer(Framebuffer, DepthAttachment, depthTex_, 0, 0);
        rhi::Gvtcier_Rhi_DrawBuffer(None);
        rhi::Gvtcier_Rhi_ReadBuffer(None);
        boundLayer_ = 0;
        boundDepthOnly_ = false;
    }
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    rhi::Gvtcier_Rhi_Viewport(0, 0, screenW, screenH);
}

// 创建点光源阴影立方体贴图。
// 与 2D 阴影图的区别：
//   - 六个面用 R32F 单通道颜色附件存“到光源的距离”，而不是用深度附件，
//     这样可以绕开立方体深度纹理在各驱动上的兼容性问题
//   - 深度比较用独立的 Depth24 渲染缓冲（RBO），只负责写深度做遮挡剔除
//   - 六个面共享同一个 FBO，渲染时由 bindFace 逐面改绑颜色附件
//   - 空面初始清成 1e9，表示“无限远、没有遮挡”
// size 为每面边长；返回 false 表示帧缓冲不完整。
bool CubeShadowMap::create(int size) {
    destroy();
    if (size <= 0) return false;
    size_ = size;

    rhi::Gvtcier_Rhi_GenTextures(1, &cubeTex_);
    rhi::Gvtcier_Rhi_BindTextureUnit(15, TexCube, cubeTex_);
    for (int f = 0; f < 6; ++f)
        rhi::Gvtcier_Rhi_TexImage2D(TexCubePosX + f, 0, R32F, size, size,
                        Red, Float, nullptr);
    rhi::Gvtcier_Rhi_TexParameteri(TexCube, TexMinFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(TexCube, TexMagFilter, Linear);
    rhi::Gvtcier_Rhi_TexParameteri(TexCube, TexWrapS, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(TexCube, TexWrapT, ClampToEdge);
    rhi::Gvtcier_Rhi_TexParameteri(TexCube, TexWrapR, ClampToEdge);

    rhi::Gvtcier_Rhi_GenFramebuffers(1, &fbo_);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_GenRenderbuffers(1, &depthRbo_);
    rhi::Gvtcier_Rhi_BindRenderbuffer(Renderbuffer, depthRbo_);
    rhi::Gvtcier_Rhi_RenderbufferStorage(Renderbuffer, Depth24, size, size);
    rhi::Gvtcier_Rhi_FramebufferRenderbuffer(Framebuffer, DepthAttachment, Renderbuffer, depthRbo_);
    rhi::Gvtcier_Rhi_BindRenderbuffer(Renderbuffer, 0);
    rhi::Gvtcier_Rhi_DrawBuffer(None);
    unsigned status = rhi::Gvtcier_Rhi_CheckFramebufferStatus(Framebuffer);
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    if (status != FramebufferComplete) {
        printf("[shadow] point framebuffer incomplete: 0x%X\n", status);
        return false;
    }
    printf("[shadow] point shadow map %dx%d ready\n", size, size);
    return true;
}

// 释放立方体阴影图的三类对象：深度渲染缓冲、帧缓冲、立方体纹理。
void CubeShadowMap::destroy() {
    if (depthRbo_) rhi::Gvtcier_Rhi_DeleteRenderbuffers(1, &depthRbo_);
    if (fbo_) rhi::Gvtcier_Rhi_DeleteFramebuffers(1, &fbo_);
    if (cubeTex_) rhi::Gvtcier_Rhi_DeleteTextures(1, &cubeTex_);
    depthRbo_ = 0;
    fbo_ = 0;
    cubeTex_ = 0;
    size_ = 0;
}

// 绑定立方体贴图的第 face 面（0~5）作为渲染目标，开颜色输出并清屏。
// 清颜色用 1e9 而不是 1.0：距离值要参与“是否被遮挡”的比较，必须远大于任何真实距离，
// 未绘制到的方向才会被判成“无遮挡”。
// 深度缓冲同时清 0（反向 Z 的最远值）。
void CubeShadowMap::bindFace(int face) {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, fbo_);
    rhi::Gvtcier_Rhi_FramebufferTexture2D(Framebuffer, ColorAttachment0,
                              TexCubePosX + face, cubeTex_, 0);
    rhi::Gvtcier_Rhi_DrawBuffer(ColorAttachment0);
    rhi::Gvtcier_Rhi_Viewport(0, 0, size_, size_);
    rhi::Gvtcier_Rhi_ClearDepth(0.0);
    rhi::Gvtcier_Rhi_ClearColor(1e9f, 0.0f, 0.0f, 1.0f);
    rhi::Gvtcier_Rhi_Clear(ColorBit | DepthBit);
}

// 解除立方体阴影图绑定，恢复默认帧缓冲并还原屏幕视口。
void CubeShadowMap::unbind(int screenW, int screenH) {
    rhi::Gvtcier_Rhi_BindFramebuffer(Framebuffer, 0);
    rhi::Gvtcier_Rhi_Viewport(0, 0, screenW, screenH);
}
