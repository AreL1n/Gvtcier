#pragma once
#include "bicu/core/export.h"
#include "bicu/core/math.h"
#include "Gvtcier3D/render/camera.h"
#include <string>

struct Scene;

struct CameraValidate {
    bool ok = false;
    bool hasPlayer = false;
    bool hasFixed = false;
    std::string reason;

    static GVTCIER_API CameraValidate Gvtcier_Camera_Check(const Scene& scene);
};
