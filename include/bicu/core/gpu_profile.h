#pragma once
#include "bicu/core/export.h"
#include "bicu/core/settings.h"

enum GpuVendor {
    GPU_VENDOR_UNKNOWN = 0,
    GPU_VENDOR_NVIDIA,
    GPU_VENDOR_AMD,
    GPU_VENDOR_INTEL,
    GPU_VENDOR_MTHREADS,
    GPU_VENDOR_APPLE,
    GPU_VENDOR_QUALCOMM,
    GPU_VENDOR_SOFTWARE
};

struct GpuProfile {
    GpuVendor vendor = GPU_VENDOR_UNKNOWN;
    bool angle = false;
    bool mthreads = false;
    bool software = false;
    bool vulkan = false;
    int glMajor = 0;
    int glMinor = 0;
    int maxTextureSize = 0;
    int maxSamples = 0;
    char vendorName[256] = { 0 };
    char rendererName[256] = { 0 };
    char versionName[256] = { 0 };
    char profileName[64] = { 0 };
};

GVTCIER_API GpuProfile& Gvtcier_Gpu();
GVTCIER_API void Gvtcier_Gpu_Detect();
GVTCIER_API void Gvtcier_Gpu_Describe(char* out, int outSize);
GVTCIER_API bool Gvtcier_Gpu_ApplyAutoTune(GameSettings& s);
