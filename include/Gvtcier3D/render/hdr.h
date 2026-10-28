#pragma once
#include "Rhidriver/rhi.h"

class ColorFbo {
public:
    GVTCIER_API bool create(int width, int height, bool withDepth);
    GVTCIER_API bool createBlur(int width, int height);
    GVTCIER_API bool createR8(int width, int height, int attachments);
    GVTCIER_API void destroy();
    GVTCIER_API void bind();
    GVTCIER_API void unbind(int screenW, int screenH);
    GVTCIER_API void setColorFilter(bool linear);
    GVTCIER_API unsigned int colorTex() const { return colorTex_; }
    GVTCIER_API unsigned int normalTex() const { return normalTex_; }
    GVTCIER_API unsigned int albedoTex() const { return albedoTex_; }
    GVTCIER_API unsigned int materialTex() const { return materialTex_; }
    GVTCIER_API unsigned int depthTex() const { return depthTex_; }
    GVTCIER_API unsigned int fbo() const { return fbo_; }
    GVTCIER_API int width() const { return width_; }
    GVTCIER_API int height() const { return height_; }

private:
    unsigned int fbo_ = 0;
    unsigned int colorTex_ = 0;
    unsigned int normalTex_ = 0;
    unsigned int albedoTex_ = 0;
    unsigned int materialTex_ = 0;
    unsigned int depthRbo_ = 0;
    unsigned int depthTex_ = 0;
    int width_ = 0;
    int height_ = 0;
};
