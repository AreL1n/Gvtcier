#pragma once
#include "Rhidriver/rhi.h"
#include "bicu/core/math.h"

class Texture {
public:
    Texture() = default;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept {
        id_ = o.id_; w_ = o.w_; h_ = o.h_;
        o.id_ = 0; o.w_ = 0; o.h_ = 0;
    }
    Texture& operator=(Texture&& o) noexcept {
        if (this != &o) {
            destroy();
            id_ = o.id_; w_ = o.w_; h_ = o.h_;
            o.id_ = 0; o.w_ = 0; o.h_ = 0;
        }
        return *this;
    }
    GVTCIER_API bool create(int w, int h, const unsigned char* rgba);
    GVTCIER_API bool createArray(int w, int h, int layers, const unsigned char* const* imgs);
    GVTCIER_API void destroy();
    GVTCIER_API void bind(int unit) const;
    GVTCIER_API unsigned int id() const { return id_; }
    GVTCIER_API int width() const { return w_; }
    GVTCIER_API int height() const { return h_; }
    GVTCIER_API int layers() const { return layers_; }

private:
    unsigned int id_ = 0;
    int w_ = 0, h_ = 0;
    int layers_ = 0;
};

GVTCIER_API Texture makeSolidTexture(int w, int h, Vec3 c);
