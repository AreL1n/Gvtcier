#include "Gvtcier.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <intrin.h>
#else
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#endif

namespace {

// 抬头显示的 z 值：90 面板底色 < 91 面板文字 < 92 准星，都压在画面之上、菜单之下。
const int kZPanelBg = 90;
const int kZPanelText = 91;
const int kZCross = 92;

std::string fmtMB(double mb);
std::string fmtGameMB(double mb);

// 版本号文案，与设置页"关于"里的版本号是两处独立维护的，改版本时要一起改。
const char* kEngineVersion = "v1.1";

// 系统信息缓存。采集一次要读 CPUID、查显卡、调 API，每帧做太浪费，
// 所以第一次绘制时抓一次，之后一直复用。
std::string gCpuName;
std::string gGlVersion;
std::string gGpuName;
bool gSysInfoReady = false;

#ifdef _WIN32
// 用 CPUID 的 0x80000002~0x80000004 三个叶子读出 48 字节的 CPU 品牌串，
// 这是 x86 上取 CPU 型号名字的标准做法，比查注册表轻。
std::string cpuBrand() {
    int regs[4] = { 0, 0, 0, 0 };
    char brand[64] = { 0 };
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] < 0x80000004u) return "Unknown CPU";
    unsigned* p = (unsigned*)brand;
    __cpuid((int*)&p[0], 0x80000002);
    __cpuid((int*)&p[4], 0x80000003);
    __cpuid((int*)&p[8], 0x80000004);
    brand[48] = '\0';
    const char* s = brand;
    while (*s == ' ') ++s;
    return std::string(s);
}

// 只采一次系统信息。显卡名字前会加显卡档位标签，档位是"通用"时不加，
// 因为那是兜底档，加进去只会让面板更难看。
void ensureSysInfo() {
    if (gSysInfoReady) return;
    gSysInfoReady = true;
    gCpuName = cpuBrand();
    Gvtcier_Gpu_Detect();
    const GpuProfile& gp = Gvtcier_Gpu();
    gGlVersion = gp.versionName;
    gGpuName = gp.rendererName;
    if (gp.profileName[0] && std::string(gp.profileName) != "通用")
        gGpuName = std::string("[") + gp.profileName + "] " + gGpuName;
}

// 拼内存用量字符串，格式为"系统已用 / 系统总量 / 本进程占用 (百分比)"。
// 百分比算的是本进程占物理内存总量的比例，不是系统占用率。
std::string memUsage() {
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return "n/a";
    PROCESS_MEMORY_COUNTERS pmc;
    memset(&pmc, 0, sizeof(pmc));
    double gameMB = 0.0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        gameMB = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    double totalMB = (double)ms.ullTotalPhys / (1024.0 * 1024.0);
    double usedMB = (double)(ms.ullTotalPhys - ms.ullAvailPhys) / (1024.0 * 1024.0);
    double pct = (totalMB > 0.0) ? (gameMB / totalMB * 100.0) : 0.0;
    char tail[32];
    snprintf(tail, sizeof(tail), " (%.1f%%)", pct);
    return fmtMB(usedMB) + " / " + fmtMB(totalMB) + " / " + fmtGameMB(gameMB) + tail;
}

// 本地时钟，显示成 "时间(2): 时:分:秒"。带"(2)"是沿用引擎早期面板的排版习惯。
std::string clockTime() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "时间(2): %02d:%02d:%02d",
             (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    return std::string(buf);
}
#else
// 非 Windows 分支：CPU 型号拿不到就写死 ARM CPU；内存用 getrusage 的峰值 RSS。
void ensureSysInfo() {
    if (gSysInfoReady) return;
    gSysInfoReady = true;
    gCpuName = "ARM CPU";
    Gvtcier_Gpu_Detect();
    const GpuProfile& gp = Gvtcier_Gpu();
    gGlVersion = gp.versionName;
    gGpuName = gp.rendererName;
}

// 非 Windows 的内存展示：三栏都填近似值，可用内存拿不到就直接重复进程占用。
std::string memUsage() {
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    getrusage(RUSAGE_SELF, &ru);
    double gameMB = (double)ru.ru_maxrss / 1024.0;
    long pages = sysconf(_SC_PHYS_PAGES);
    long psize = sysconf(_SC_PAGE_SIZE);
    double totalMB = (pages > 0) ? (double)pages * psize / (1024.0 * 1024.0) : 0.0;
    char buf[128];
    snprintf(buf, sizeof(buf), "%.1f MB / %.1f MB / %.1f MB", gameMB, totalMB, gameMB);
    return std::string(buf);
}

std::string clockTime() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm lt;
    time_t t = tv.tv_sec;
    localtime_r(&t, &lt);
    char buf[32];
    snprintf(buf, sizeof(buf), "时间(2): %02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
    return std::string(buf);
}
#endif

// 截断字符串到 maxLen，尽量在最后一个空格处断开，再补省略号，避免截出半个单词。
std::string trimTo(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    std::string out = s.substr(0, maxLen);
    size_t sp = out.rfind(' ');
    if (sp != std::string::npos && sp > maxLen / 2) out = out.substr(0, sp);
    return out + "...";
}

// 大于 1GB 换成 GB 显示，否则按 MB，用于系统总量/已用量。
std::string fmtMB(double mb) {
    char buf[64];
    if (mb >= 1024.0) snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    else snprintf(buf, sizeof(buf), "%.1f MB", mb);
    return std::string(buf);
}

// 进程占用的显示。小于 1MB 时降到 KB，因为引擎自身占用通常就几百 KB，用 MB 会全显示成 0.0。
std::string fmtGameMB(double mb) {
    char buf[64];
    if (mb >= 1024.0) snprintf(buf, sizeof(buf), "%.1f GB", mb / 1024.0);
    else if (mb >= 1.0) snprintf(buf, sizeof(buf), "%.1f MB", mb);
    else snprintf(buf, sizeof(buf), "%.1f KB", mb * 1024.0);
    return std::string(buf);
}

// 简易 printf 包装，返回 std::string，方便和 "+" 拼字符串。缓冲 256 字节，长文本会被截断。
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

}

// 累积帧时间，每满 0.5 秒结算一次平均帧率与平均帧时间。
// 用 0.5 秒窗口是因为太短数字跳得厉害，太长又跟不上卡顿；
// 非法 dt（<=0 或 >1 秒，通常是窗口拖动/暂停造成的尖峰）直接丢弃，避免把平均值带崩。
void Hud::Gvtcier_Hud_Update(float frameDt) {
    if (frameDt <= 0.0f || frameDt > 1.0f) return;
    accum_ += frameDt;
    ++frames_;
    if (accum_ >= 0.5f) {
        fps_ = (float)frames_ / accum_;
        ms_ = accum_ * 1000.0f / (float)frames_;
        accum_ = 0.0f;
        frames_ = 0;
    }
}

// 绘制抬头显示。
// 先画准星：四根白色短臂 + 中心 2x2 黑点，每根臂外圈垫一层半透明黑边，
// 保证在浅色背景上也能看清。臂长 16、间隙 4、线宽 2，这些是长期调出来的观感尺寸。
// 再画面板：两列信息，左列帧率/游戏时长/本地时钟/图形 API，右列帧时间/坐标/内存/CPU，
// 最后一整行放 GPU。面板宽高根据文本实测宽度动态算，避免长显卡名被裁。
// 内存字符串每 0.5 秒刷新一次，因为它要调系统 API，没必要每帧查。
void Hud::Gvtcier_Hud_Draw(Renderer& r, int w, int h, const HudStats& st,
                           bool showPanel, bool showCrosshair) {
    ensureSysInfo();
    const float s = 0.62f;
    const float lineH = 22.0f;

    if (showCrosshair) {
        float cx = (float)w * 0.5f;
        float cy = (float)h * 0.5f;
        const float len = 16.0f;
        const float gap = 4.0f;
        const float th = 2.0f;
        const float edge = 1.0f;

        auto bar = [&](float bx, float by, float bw, float bh) {
            r.Gvtcier_Render_DrawRect2D(bx - edge, by - edge, bw + edge * 2.0f, bh + edge * 2.0f,
                                        0.0f, 0.0f, 0.0f, 0.55f, kZCross);
            r.Gvtcier_Render_DrawRect2D(bx, by, bw, bh, 0.96f, 0.97f, 0.98f, 0.92f, kZCross + 1);
        };

        bar(cx - gap - len, cy - th * 0.5f, len, th);
        bar(cx + gap, cy - th * 0.5f, len, th);
        bar(cx - th * 0.5f, cy - gap - len, th, len);
        bar(cx - th * 0.5f, cy + gap, th, len);
        r.Gvtcier_Render_DrawRect2D(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.5f, kZCross);
    }

    if (!showPanel) return;

    static std::string memCache;
    static double memNext = 0.0;
    double nowSec = Gvtcier_NowSeconds();
    if (nowSec >= memNext) {
        memCache = memUsage();
        memNext = nowSec + 0.5;
    }

    std::string col1[4];
    col1[0] = fmt("帧率: %.1f", fps_);
    int mins = (int)st.elapsed / 60;
    int secs = (int)st.elapsed % 60;
    col1[1] = fmt("时间: %02d:%02d", mins, secs);
    col1[2] = clockTime();
    col1[3] = (Gvtcier_Gpu().vulkan ? "Vulkan: " : "OpenGL: ") + trimTo(gGlVersion, 34);

    std::string col2[4];
    col2[0] = fmt("帧时间: %.2f ms", ms_);
    col2[1] = fmt("X: %.1f  Y: %.1f  Z: %.1f", st.px, st.py, st.pz);
    col2[2] = "内存: " + memCache;
    col2[3] = "CPU: " + trimTo(gCpuName, 28);

    std::string gpuLine = "GPU: " + trimTo(gGpuName, 40);

    const int c1x = 16;
    int y = 14;
    const int pad = 10;
    const int colGap = 24;

    float col1W = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float tw = 0.0f, th = 0.0f;
        r.Gvtcier_Render_MeasureText(col1[i].c_str(), s, &tw, &th);
        col1W = (std::max)(col1W, tw);
    }
    float col2W = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float tw = 0.0f, th = 0.0f;
        r.Gvtcier_Render_MeasureText(col2[i].c_str(), s, &tw, &th);
        col2W = (std::max)(col2W, tw);
    }
    float gpuW = 0.0f;
    r.Gvtcier_Render_MeasureText(gpuLine.c_str(), s, &gpuW, nullptr);

    const int c2x = c1x + (int)col1W + colGap;
    int textW = (int)((float)(c2x - c1x) + col2W);
    if ((int)gpuW > textW) textW = (int)gpuW;
    int panelW = pad * 2 + textW + pad;
    int panelH = (int)(lineH * 5.0f) + pad * 2;

    r.Gvtcier_Render_DrawRect2D((float)(c1x - pad), (float)(y - pad), (float)panelW, (float)panelH,
                                0.05f, 0.06f, 0.07f, 0.62f, kZPanelBg);

    for (int i = 0; i < 4; ++i) {
        r.Gvtcier_Render_DrawText(c1x, y + (int)(lineH * i), s, col1[i].c_str(), kZPanelText);
        r.Gvtcier_Render_DrawText(c2x, y + (int)(lineH * i), s, col2[i].c_str(), kZPanelText);
    }
    r.Gvtcier_Render_DrawText(c1x, y + (int)(lineH * 4.0f), s, gpuLine.c_str(), kZPanelText);
}
