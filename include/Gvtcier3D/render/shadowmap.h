#pragma once
#include "Rhidriver/rhi.h"
#include <cstdio>

class ShadowMap {
public:
    GVTCIER_API bool create(int size);
    GVTCIER_API bool createLayered(int size, int layers);
    GVTCIER_API void destroy();
    GVTCIER_API void bind();
    GVTCIER_API void bindLayer(int layer);
    GVTCIER_API void bindLayerDepthOnly(int layer);
    GVTCIER_API void unbind(int screenW, int screenH);
    GVTCIER_API unsigned int depthTex() const { return depthTex_; }
    GVTCIER_API unsigned int fbo() const { return fbo_; }
    GVTCIER_API bool layered() const { return layered_; }
    GVTCIER_API int layers() const { return layers_; }
    GVTCIER_API int size() const { return size_; }

private:
    unsigned int fbo_ = 0;
    unsigned int depthTex_ = 0;
    int size_ = 0;
    bool layered_ = false;
    int layers_ = 1;
    int boundLayer_ = 0;
    bool boundDepthOnly_ = false;
};

class CubeShadowMap {
public:
    GVTCIER_API bool create(int size);
    GVTCIER_API void destroy();
    GVTCIER_API void bindFace(int face);
    GVTCIER_API void unbind(int screenW, int screenH);
    GVTCIER_API unsigned int cubeTex() const { return cubeTex_; }
    GVTCIER_API int size() const { return size_; }

private:
    unsigned int fbo_ = 0;
    unsigned int cubeTex_ = 0;
    unsigned int depthRbo_ = 0;
    int size_ = 0;
};
