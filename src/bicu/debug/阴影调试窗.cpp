#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "Gvtcier.h"
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>

#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

// 阴影调参窗口：开发期工具，实时调整级联阴影的偏移参数并立刻看效果。
// 引擎侧只暴露三个入口（Gvtcier_Shadow_Tuning / _Probe / _ProbeCursor），
// 渲染器每帧读取这里的值去设置阴影 uniform，所以改动无需重编译着色器。
// g_tune 的初始值就是引擎的默认阴影参数，字段含义见 ShadowTuning 定义。
// 字段顺序：lightBias, normalBias, slopeClamp, diskRadius, pcfRadius, zPad,
//           splitLambda, biasScale, blendBand, debug, debugLayer,
//           singleTap, showTexelGrid, showWorldTexel
// singleTap 默认 0（走完整 PCF 软阴影）；置 1 会跳过所有滤波只取单次硬比较，
// 那条路径在 shader 里是提前 return 的，PCF 相关改动将完全看不到效果。
static ShadowTuning g_tune{ 0.0f, 10.0f, 0.0f, 10.0f, 0.87f, 0.0f, 0.6f, 1.79f, 0.1f, 0, 0, 0, 0, 0 };
static ShadowProbe g_probe{};   // 当前鼠标所指像素的阴影中间量，窗口里显示这些值方便排查漏光

ShadowTuning& Gvtcier_Shadow_Tuning() { return g_tune; }
ShadowProbe& Gvtcier_Shadow_Probe() { return g_probe; }

// 主循环每帧写入鼠标坐标，渲染器在 shadowProbePass 里据此回读一个像素的阴影数据
int* Gvtcier_Shadow_ProbeCursor() {
    static int c[2] = {0, 0};
    return c;
}

struct ParamSpec {
    const wchar_t* label;
    float vmin, vmax;
    int sliderId, valueId;
    int fieldIndex;
};

static const ParamSpec g_specs[] = {
    { L"沿光偏移(倍 range)", 0.0f, 10.0f, 2001, 2101, 0 },
    { L"法线偏移(倍 texel)", 0.0f, 10.0f, 2002, 2102, 1 },
    { L"斜率补偿倍率", 0.0f, 10.0f, 2003, 2103, 2 },
    { L"采样盘半径", 0.0f, 10.0f, 2004, 2104, 3 },
    { L"PCF 半径倍率", 0.0f, 10.0f, 2005, 2105, 4 },
    { L"深度附加量(世界)", 0.0f, 10.0f, 2006, 2106, 5 },
    { L"级联过渡带", 0.0f, 10.0f, 2007, 2107, 8 },
    { L"接收端偏移倍率", 0.0f, 10.0f, 2008, 2108, 7 },
};

static const int g_specCount = (int)(sizeof(g_specs) / sizeof(g_specs[0]));
static const int g_dbgBtnId = 2201;
static const int g_resetBtnId = 2202;
static const int g_tapBtnId = 2203;
static const int g_gridBtnId = 2204;
static const int g_worldTexelBtnId = 2205;
static const int g_layerBtnId = 2206;
static const int g_probeId = 2301;
static HWND g_hwnd = nullptr;

static float* fieldPtr(ShadowTuning& t, int idx) {
    switch (idx) {
    case 0: return &t.lightBias;
    case 1: return &t.normalBias;
    case 2: return &t.slopeClamp;
    case 3: return &t.diskRadius;
    case 4: return &t.pcfRadius;
    case 5: return &t.zPad;
    case 6: return &t.splitLambda;
    case 7: return &t.biasScale;
    case 8: return &t.blendBand;
    }
    return &t.lightBias;
}

static float posToValue(int idx, int pos) {
    const ParamSpec& s = g_specs[idx];
    return s.vmin + (s.vmax - s.vmin) * (float)pos / 1000.0f;
}

static int valueToPos(int idx, float v) {
    const ParamSpec& s = g_specs[idx];
    float t = (v - s.vmin) / (s.vmax - s.vmin);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (int)(t * 1000.0f + 0.5f);
}

static void updateValueText(HWND dlg, int idx, float v) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.5f", v);
    SetWindowTextW(GetDlgItem(dlg, g_specs[idx].valueId), buf);
}

static const wchar_t* debugName(int d) {
    switch (d) {
    case 1: return L"调试图:深度差(q.z-d)";
    case 2: return L"调试图:阴影遮罩";
    case 3: return L"调试图:级联着色";
    case 4: return L"调试图:贴图UV";
    case 5: return L"调试图:原始深度";
    case 6: return L"调试图:深度跳变";
    case 7: return L"调试图:texel网格";
    case 8: return L"调试图:世界texel";
    case 9: return L"探针:深度数值";
    case 10: return L"探针:bias数值";
    default: return L"调试图:关";
    }
}

static void syncAll(HWND dlg) {
    for (int i = 0; i < g_specCount; ++i) {
        float v = *fieldPtr(g_tune, g_specs[i].fieldIndex);
        SendMessageW(GetDlgItem(dlg, g_specs[i].sliderId), TBM_SETPOS, TRUE, valueToPos(i, v));
        updateValueText(dlg, i, v);
    }
    SetWindowTextW(GetDlgItem(dlg, g_dbgBtnId), debugName(g_tune.debug));
    wchar_t buf[64];
    swprintf(buf, 64, L"级联层: %d", g_tune.debugLayer);
    SetWindowTextW(GetDlgItem(dlg, g_layerBtnId), buf);
    SetWindowTextW(GetDlgItem(dlg, g_tapBtnId), g_tune.singleTap ? L"单采样:开(内建)" : L"单采样:关");
    SetWindowTextW(GetDlgItem(dlg, g_gridBtnId), g_tune.showTexelGrid ? L"texel网格:开" : L"texel网格:关");
    SetWindowTextW(GetDlgItem(dlg, g_worldTexelBtnId), g_tune.showWorldTexel ? L"世界texel:开" : L"世界texel:关");
}

static void resetDefaults(HWND dlg) {
    g_tune.lightBias = 0.0f;
    g_tune.normalBias = 10.0f;
    g_tune.slopeClamp = 0.0f;
    g_tune.diskRadius = 10.0f;
    g_tune.pcfRadius = 0.87f;
    g_tune.zPad = 0.0f;
    g_tune.splitLambda = 0.6f;
    g_tune.biasScale = 1.79f;
    g_tune.blendBand = 0.1f;
    g_tune.debug = 0;
    g_tune.debugLayer = 0;
    g_tune.singleTap = 0;
    g_tune.showTexelGrid = 0;
    g_tune.showWorldTexel = 0;
    syncAll(dlg);
}

static void updateProbeText() {
    if (!g_hwnd) return;
    wchar_t buf[512];
    if (g_probe.valid < 0.5f) {
        swprintf(buf, 512, L"探针: 光标不在可见表面");
    } else {
        swprintf(buf, 512,
                 L"级联=%d\n"
                 L"q.z   = %.6f\nd     = %.6f\nq.z-d = %+.6f\n"
                 L"texel世界=%.5f\n深度跨度=%.3f\n"
                 L"slopeTan=%.3f\n法线偏移=%.5f\n深度偏移=%.5f",
                 (int)g_probe.casc, g_probe.qz, g_probe.dc, g_probe.diff,
                 g_probe.texelWorld, g_probe.range,
                 g_probe.slopeTan, g_probe.normalBiasAmt, g_probe.depthBiasAmt);
    }
    SetWindowTextW(GetDlgItem(g_hwnd, g_probeId), buf);
}

static LRESULT CALLBACK tuneProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HSCROLL: {
        HWND src = (HWND)lp;
        for (int i = 0; i < g_specCount; ++i) {
            if (src == GetDlgItem(wnd, g_specs[i].sliderId)) {
                int pos = (int)SendMessageW(src, TBM_GETPOS, 0, 0);
                float v = posToValue(i, pos);
                *fieldPtr(g_tune, g_specs[i].fieldIndex) = v;
                updateValueText(wnd, i, v);
                return 0;
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (HIWORD(wp) != BN_CLICKED) break;
        if (id == g_dbgBtnId) {
            g_tune.debug = (g_tune.debug + 1) % 11;
            syncAll(wnd);
            return 0;
        }
        if (id == g_resetBtnId) { resetDefaults(wnd); return 0; }
        if (id == g_tapBtnId) {
            g_tune.singleTap = g_tune.singleTap ? 0 : 1;
            syncAll(wnd);
            return 0;
        }
        if (id == g_gridBtnId) {
            g_tune.showTexelGrid = g_tune.showTexelGrid ? 0 : 1;
            syncAll(wnd);
            return 0;
        }
        if (id == g_worldTexelBtnId) {
            g_tune.showWorldTexel = g_tune.showWorldTexel ? 0 : 1;
            syncAll(wnd);
            return 0;
        }
        if (id == g_layerBtnId) {
            g_tune.debugLayer = (g_tune.debugLayer + 1) % 4;
            syncAll(wnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;
    case WM_DESTROY:
        g_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

static DWORD WINAPI tuneThread(LPVOID) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = tuneProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GvtShadowTuning";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    int rowH = 52;
    int w = 380, h = 20 + g_specCount * rowH + 240;
    RECT rc{ 0, 0, w, h };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST, L"GvtShadowTuning", L"阴影调试",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE,
        40, 60, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);

    for (int i = 0; i < g_specCount; ++i) {
        int y = 8 + i * rowH;
        CreateWindowExW(0, L"STATIC", g_specs[i].label, WS_CHILD | WS_VISIBLE,
            10, y, 220, 16, g_hwnd, (HMENU)(intptr_t)(1000 + i), wc.hInstance, nullptr);
        CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            10, y + 18, 290, 26, g_hwnd, (HMENU)(intptr_t)g_specs[i].sliderId, wc.hInstance, nullptr);
        SendMessageW(GetDlgItem(g_hwnd, g_specs[i].sliderId), TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
        CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_CENTER,
            306, y, 64, 20, g_hwnd, (HMENU)(intptr_t)g_specs[i].valueId, wc.hInstance, nullptr);
    }
    int by = 8 + g_specCount * rowH;
    CreateWindowExW(0, L"BUTTON", L"调试图:关", WS_CHILD | WS_VISIBLE,
        10, by, 175, 26, g_hwnd, (HMENU)(intptr_t)g_dbgBtnId, wc.hInstance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"恢复默认", WS_CHILD | WS_VISIBLE,
        195, by, 175, 26, g_hwnd, (HMENU)(intptr_t)g_resetBtnId, wc.hInstance, nullptr);

    by += 32;
    CreateWindowExW(0, L"BUTTON", L"级联层: 0", WS_CHILD | WS_VISIBLE,
        10, by, 115, 26, g_hwnd, (HMENU)(intptr_t)g_layerBtnId, wc.hInstance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"单采样:关", WS_CHILD | WS_VISIBLE,
        130, by, 115, 26, g_hwnd, (HMENU)(intptr_t)g_tapBtnId, wc.hInstance, nullptr);
    CreateWindowExW(0, L"BUTTON", L"texel网格:关", WS_CHILD | WS_VISIBLE,
        250, by, 120, 26, g_hwnd, (HMENU)(intptr_t)g_gridBtnId, wc.hInstance, nullptr);

    by += 32;
    CreateWindowExW(0, L"BUTTON", L"世界texel:开", WS_CHILD | WS_VISIBLE,
        10, by, 175, 26, g_hwnd, (HMENU)(intptr_t)g_worldTexelBtnId, wc.hInstance, nullptr);
    CreateWindowExW(0, L"STATIC", L"光标探针（把鼠标移到问题位置）", WS_CHILD | WS_VISIBLE,
        195, by + 4, 175, 18, g_hwnd, nullptr, wc.hInstance, nullptr);

    by += 32;
    CreateWindowExW(0, L"STATIC", L"探针: 等待数据", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT,
        10, by, 360, 150, g_hwnd, (HMENU)(intptr_t)g_probeId, wc.hInstance, nullptr);

    syncAll(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void Gvtcier_Shadow_Tuning_Init() {
#if !GVTCIER_CSM_TUNING
    return;
#endif
    static bool started = false;
    if (started) return;
    started = true;
    HANDLE th = CreateThread(nullptr, 0, tuneThread, nullptr, 0, nullptr);
    if (th) CloseHandle(th);
}

void Gvtcier_Shadow_Tuning_RefreshProbe() {
    static ShadowProbe last{};
    static int tick = 0;
    if (++tick % 6 != 0) return;
    if (g_probe.valid == last.valid && g_probe.qz == last.qz && g_probe.dc == last.dc) return;
    last = g_probe;
    updateProbeText();
}
