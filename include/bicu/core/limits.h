#pragma once
#include "bicu/core/export.h"

struct RenderLimits {
    int maxDynamicLights = 1024;
    int maxPersistentLights = 1024;
    int maxDirLights = 8;
    int maxParticles = 8192;
    int maxBeams = 1024;
    int tileSize = 16;
    int maxLightsPerTile = 1024;
    int lightQuality = 2;
};

GVTCIER_API RenderLimits& Gvtcier_Limits();
GVTCIER_API void Gvtcier_Limits_ApplyQuality(int lightQuality);
