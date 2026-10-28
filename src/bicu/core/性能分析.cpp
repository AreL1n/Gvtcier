#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {

//全局 GPU 画像。检测一次后常驻，供渲染器与设置界面查询。
GpuProfile gGpu;
//是否已检测过。检测只做一次，避免每帧重复查询驱动字符串。
bool gDetected = false;

//受限长度的字符串拷贝，保证结果一定以 0 结尾，不会写出缓冲区。
void copyTrim(char* dst, int dstSize, const char* src) {
    if (dstSize <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    int i = 0;
    while (src[i] && i < dstSize - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

//不区分大小写的子串查找。驱动返回的厂商/渲染器字符串大小写不统一，必须忽略大小写才能可靠识别。
bool containsCI(const char* hay, const char* needle) {
    if (!hay || !needle || !needle[0]) return false;
    size_t nlen = strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) ++i;
        if (i == nlen) return true;
    }
    return false;
}

//根据厂商字符串与渲染器字符串判断 GPU 厂商，并按需打上特殊标记。
//摩尔线程有多种写法（mthreads / moore threads / musa / MTT S 系列，以及 PCI 厂商号 1ed5），都要覆盖；
//NVIDIA 与 AMD 额外认 PCI 厂商号 0x10de / 0x1002，因为部分驱动只报数字 ID。
//软件渲染（llvmpipe/softpipe/SwiftShader/微软基础渲染）单独标成软件厂商，并把 software 置真，
//这是后面自动降画质的主要触发条件。
void detectVendor(GpuProfile& g) {
    if (containsCI(g.vendorName, "mthreads") || containsCI(g.rendererName, "mthreads") ||
        containsCI(g.vendorName, "moore threads") || containsCI(g.rendererName, "moore threads") ||
        containsCI(g.vendorName, "1ed5") || containsCI(g.rendererName, "1ed5") ||
        containsCI(g.vendorName, "musa") || containsCI(g.rendererName, "musa") ||
        containsCI(g.rendererName, "mtt s")) {
        g.vendor = GPU_VENDOR_MTHREADS;
        g.mthreads = true;
        return;
    }
    if (containsCI(g.vendorName, "nvidia") || containsCI(g.rendererName, "nvidia") ||
        containsCI(g.vendorName, "0x10de")) {
        g.vendor = GPU_VENDOR_NVIDIA;
        return;
    }
    if (containsCI(g.vendorName, "amd") || containsCI(g.rendererName, "amd") ||
        containsCI(g.vendorName, "ati") || containsCI(g.vendorName, "0x1002")) {
        g.vendor = GPU_VENDOR_AMD;
        return;
    }
    if (containsCI(g.vendorName, "intel") || containsCI(g.rendererName, "intel") ||
        containsCI(g.vendorName, "0x8086")) {
        g.vendor = GPU_VENDOR_INTEL;
        return;
    }
    if (containsCI(g.vendorName, "apple") || containsCI(g.rendererName, "apple")) {
        g.vendor = GPU_VENDOR_APPLE;
        return;
    }
    if (containsCI(g.vendorName, "qualcomm") || containsCI(g.rendererName, "adreno")) {
        g.vendor = GPU_VENDOR_QUALCOMM;
        return;
    }
    if (containsCI(g.rendererName, "llvmpipe") || containsCI(g.rendererName, "softpipe") ||
        containsCI(g.rendererName, "swiftshader") || containsCI(g.rendererName, "microsoft basic render")) {
        g.vendor = GPU_VENDOR_SOFTWARE;
        g.software = true;
    }
}

//把厂商枚举转成短标签，用于日志与诊断信息展示。
const char* vendorLabel(GpuVendor v) {
    switch (v) {
        case GPU_VENDOR_NVIDIA: return "NVIDIA";
        case GPU_VENDOR_AMD: return "AMD";
        case GPU_VENDOR_INTEL: return "Intel";
        case GPU_VENDOR_MTHREADS: return "MooreThreads";
        case GPU_VENDOR_APPLE: return "Apple";
        case GPU_VENDOR_QUALCOMM: return "Qualcomm";
        case GPU_VENDOR_SOFTWARE: return "Software";
        default: return "Unknown";
    }
}

}

//取 GPU 画像（只读用途，实际可写）。
GpuProfile& Gvtcier_Gpu() { return gGpu; }

//检测 GPU 能力，只在首次调用时真正执行。
//数据来源是当前图形上下文：厂商与渲染器字符串、GL 版本、最大纹理尺寸与最大采样数。
//必须在 RHI 初始化之后调用，否则查不到任何字符串。
//副产物：给出一份 profileName 用于标识"这类显卡按哪种策略调画质"，ANGLE 与软件渲染会被单独命名。
void Gvtcier_Gpu_Detect() {
    if (gDetected) return;
    gDetected = true;
    const char* v = (const char*)rhi::Gvtcier_Rhi_GetString(rhi::VendorName);
    const char* r = (const char*)rhi::Gvtcier_Rhi_GetString(rhi::RendererName);
    const char* ver = (const char*)rhi::Gvtcier_Rhi_GetString(rhi::Version);
    copyTrim(gGpu.vendorName, sizeof(gGpu.vendorName), v);
    copyTrim(gGpu.rendererName, sizeof(gGpu.rendererName), r);
    copyTrim(gGpu.versionName, sizeof(gGpu.versionName), ver);
    gGpu.angle = containsCI(gGpu.rendererName, "angle");
    gGpu.vulkan = (rhi::Gvtcier_Rhi_Backend() == rhi::Backend::Vulkan);
    rhi::Gvtcier_Rhi_GetIntegerv(rhi::MajorVersion, &gGpu.glMajor);
    rhi::Gvtcier_Rhi_GetIntegerv(rhi::MinorVersion, &gGpu.glMinor);
    rhi::Gvtcier_Rhi_GetIntegerv(rhi::MaxTextureSize, &gGpu.maxTextureSize);
    rhi::Gvtcier_Rhi_GetIntegerv(rhi::MaxSamples, &gGpu.maxSamples);
    detectVendor(gGpu);
    //画质档位的分类名：摩尔线程最优先判断，其次软件渲染，再次 ANGLE 转译，都不命中即为通用档。
    const char* profile = "通用";
    if (gGpu.mthreads) profile = "摩尔线程";
    else if (gGpu.software) profile = "软件渲染";
    else if (gGpu.angle) profile = "ANGLE 转译";
    copyTrim(gGpu.profileName, sizeof(gGpu.profileName), profile);
}

//把 GPU 信息拼成一行可读诊断串写入 out（outSize 为缓冲区字节数），格式：厂商 | 渲染器 | GL 版本 | 档位。
//输出保证以 0 结尾，超长会截断。
void Gvtcier_Gpu_Describe(char* out, int outSize) {
    if (!out || outSize <= 0) return;
    Gvtcier_Gpu_Detect();
    snprintf(out, (size_t)outSize, "%s | %s | GL %d.%d | %s",
             vendorLabel(gGpu.vendor), gGpu.rendererName,
             gGpu.glMajor, gGpu.glMinor, gGpu.profileName);
}

//按显卡类型自动下调画质，避免弱后端一进游戏就卡成幻灯片。
//策略是只降不升：仅在当前值高于该档位上限时才下调，玩家自选的更省配置不会被反向拉高。
//返回 true 表示确实改动了某些项，调用方据此决定是否提示与保存。
//三档策略：
//  软件渲染  一切能关的都关（阴影/光照/雾降最低，体积光、HDR、高光关，各向异性关），否则帧率无法接受；
//  摩尔线程  体积光关（其驱动上开销过大），阴影与光照降到中，各向异性最多 2；
//  ANGLE     体积光关（转译层上开销大），各向异性最多 2，其余保持玩家设置。
bool Gvtcier_Gpu_ApplyAutoTune(GameSettings& s) {
    Gvtcier_Gpu_Detect();
    bool changed = false;

    if (gGpu.software) {
        //软件渲染没有硬件加速，必须把每像素开销压到最低；各向异性 1 即等价于关闭过滤。
        if (s.shadowQuality != SHADOW_LOW) { s.shadowQuality = SHADOW_LOW; changed = true; }
        if (s.lightQuality != LIGHT_LOW) { s.lightQuality = LIGHT_LOW; changed = true; }
        if (s.fogQuality != FOG_LOW) { s.fogQuality = FOG_LOW; changed = true; }
        if (s.volumeLight) { s.volumeLight = false; changed = true; }
        if (s.hdr) { s.hdr = false; changed = true; }
        if (s.specular) { s.specular = false; changed = true; }
        if (s.anisotropic > 1) { s.anisotropic = 1; changed = true; }
        return changed;
    }

    if (gGpu.mthreads) {
        //摩尔线程的已知短板是体积光与高档阴影，其余保持玩家选择，尽量减少对画面的影响。
        if (s.volumeLight) { s.volumeLight = false; changed = true; }
        if (s.shadowQuality > SHADOW_MEDIUM) { s.shadowQuality = SHADOW_MEDIUM; changed = true; }
        if (s.lightQuality > LIGHT_MEDIUM) { s.lightQuality = LIGHT_MEDIUM; changed = true; }
        if (s.anisotropic > 2) { s.anisotropic = 2; changed = true; }
        return changed;
    }

    if (gGpu.angle) {
        //ANGLE 把 GL 调用转译到别的图形 API，体积光这类多重渲染目标开销被放大，因此单独关掉。
        if (s.volumeLight) { s.volumeLight = false; changed = true; }
        if (s.anisotropic > 2) { s.anisotropic = 2; changed = true; }
        return changed;
    }

    return changed;
}
