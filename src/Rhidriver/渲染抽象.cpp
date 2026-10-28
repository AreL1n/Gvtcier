#include "Gvtcier.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// RHI 抽象层的唯一实现：本文件不直接调用任何图形 API，
// 只负责“挑一张后端函数表”并把上层 Gvtcier_Rhi_* 调用转发过去。
// GL / GLES / Vulkan 三者在编译期各自提供一张 RhiTable（见 backend.h），
// 运行期由 g_table 指向其中一个，因此上层代码对后端完全无感。
namespace rhi {

// 当前生效的后端函数表；为 nullptr 表示尚未初始化或已关闭，此时所有 RHI 调用都是空操作。
static RhiTable* g_table = nullptr;
// 当前后端枚举，供 Gvtcier_Rhi_Backend 查询。
static Backend g_backend = Backend::Gl;
// 渲染窗口对象，GL 分支下新建，GLES 分支下引用外部传入的窗口。
static GvWWindow* g_rhiWin = nullptr;

#ifdef GVWCIER_USE_GLES
// GLES 平台上由宿主程序（如 Android 的 Java/原生层）传入的窗口，Gvtcier_Rhi_Init 时取用。
static GvWWindow* g_rhiWinExternal = nullptr;

// 记录外部原生窗口，必须在 Gvtcier_Rhi_Init 之前调用，
// 因为 GLES 的绘制表面不是 Win32 HWND，无法通过 Init 的参数传递。
void Gvtcier_Rhi_SetWindow(void* w) {
    g_rhiWinExternal = (GvWWindow*)w;
}
#endif

// 尺寸变化桥接：有后端表时转发给后端的 resize。
static void rhiResizeBridge(int w, int h) {
    if (g_table) g_table->resize(w, h);
}

// 初始化渲染后端。
// hwnd：窗口句柄，仅桌面 GL 分支使用。
// 返回值：成功选出后端返回 true，否则 false。
// 若已有后端在运行，会先关闭再重建，避免重复创建上下文导致资源泄漏。
bool Gvtcier_Rhi_Init(HWND hwnd) {
    (void)hwnd;
    if (g_table) { g_table->shutdown(); g_table = nullptr; }
#ifdef GVWCIER_USE_GLES
    // GLES 构建：只认外部登记的原生窗口，没有窗口就直接失败，不退回 GL。
    g_rhiWin = g_rhiWinExternal;
    if (g_rhiWin) {
        g_table = glesTable(g_rhiWin);
        if (g_table) {
            g_backend = Backend::Gles;
            printf("[rhi] gles backend selected\n");
            fflush(stdout);
            return true;
        }
    }
    printf("[rhi] no backend available\n");
    fflush(stdout);
    return false;
#else
    // 桌面构建：创建窗口对象，然后选后端。
    g_rhiWin = new GvWWindow();
    // 默认走 OpenGL，稳定优先。sokol 那条 Vulkan 通道还没跑通，
    // 想试试的话设环境变量 GVTCIER_RHI=vk 即可。
    const char* want = std::getenv("GVTCIER_RHI");
    bool wantVk = want && (std::strcmp(want, "vk") == 0 || std::strcmp(want, "vulkan") == 0);
    if (wantVk) {
        g_table = sgTable(hwnd);
        if (g_table) {
            g_backend = Backend::Vulkan;
            printf("[rhi] sokol vulkan backend selected\n");
            fflush(stdout);
            return true;
        }
        printf("[rhi] sokol vulkan backend failed, falling back to gl\n");
        fflush(stdout);
    }
    g_table = glTable(hwnd);
    if (g_table) {
        g_backend = Backend::Gl;
        printf("[rhi] gl backend selected\n");
        fflush(stdout);
        return true;
    }
    printf("[rhi] no backend available\n");
    fflush(stdout);
    return false;
#endif
}

// 返回当前后端枚举：Gl = 桌面 OpenGL 4.6 核心，Gles = OpenGL ES，Vulkan = Vulkan。
// 上层据此决定着色器语言版本、可选特性开关等分支逻辑。
Backend Gvtcier_Rhi_Backend() { return g_backend; }

// 无后端时返回的默认值：数值类型返回 0/false，void 直接什么也不做。
template <typename T> struct RhiZero { static T get() { return T{}; } };
template <> struct RhiZero<void> { static void get() {} };

// 用宏把 RHI_TABLE_ENTRIES 里列出的每个函数生成为转发函数：
// 先判空表，再从表里取同名成员调用，参数原样透传。
// 这样上层永远看到 Gvtcier_Rhi_Xxx，实际实现由当前表决定。
#define RHI_DISPATCH(ret, name, params, args) \
    ret name params { if (!g_table) return RhiZero<ret>::get(); return g_table->name args; }
RHI_TABLE_ENTRIES(RHI_DISPATCH)
#undef RHI_DISPATCH

// 以下四个函数在表里有独立成员，没有走条目清单，需要单独转发。
// 提交当前帧并交换缓冲。
void Gvtcier_Rhi_Present() { if (g_table) g_table->present(); }
// 设置垂直同步。
void Gvtcier_Rhi_SetVsync(bool v) { if (g_table) g_table->setVsync(v); }
// 通知尺寸变化。
void Gvtcier_Rhi_Resize(int w, int h) { if (g_table) g_table->resize(w, h); }
// 关闭后端并把函数表置空，之后的 RHI 调用全部变成空操作。
void Gvtcier_Rhi_Shutdown() { if (g_table) { g_table->shutdown(); g_table = nullptr; } }
// 但说实话 Vulkan 非常令人崩溃
}
