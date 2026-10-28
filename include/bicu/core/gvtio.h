#pragma once
#include "bicu/core/export.h"

enum GvtioMode {
    Gvtio_None = 0,
    Gvtio_2D = 1,
    Gvtio_3D = 2
};

GVTCIER_API void Gvtcier_Gvtio_Use2D();
GVTCIER_API void Gvtcier_Gvtio_Use3D();
GVTCIER_API bool Gvtcier_Gvtio_Is2D();
GVTCIER_API bool Gvtcier_Gvtio_Is3D();
GVTCIER_API int Gvtcier_Gvtio_Mode();
