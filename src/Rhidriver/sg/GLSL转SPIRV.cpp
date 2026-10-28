#include "Rhidriver/sg/glsl2spv.h"
#include "Rhidriver/rhi.h"

#include <windows.h>

#include <cstring>
#include <mutex>

// 着色器编译走 warehouse.dll。那个 DLL 是 MSVC 编的，内部静态链进了 glslang 全套，
// 对外只露几个纯 C 函数。MinGW 这边只管按 C 约定取函数指针，不碰任何 C++ 类型，
// 之前直接吃 SDK 里那份 glslang.dll 会崩在 shader_parse，就是 C 接口对不上的缘故。
namespace rhi {
namespace sg {

namespace {

typedef int (*WhInitFn)();
typedef int (*WhCompileFn)(int stage, const char* source, int length,
                           unsigned int** outWords, int* outCount, const char** outLog);
typedef void (*WhFreeFn)(void* p);

struct Warehouse {
    HMODULE module = nullptr;
    WhInitFn init = nullptr;
    WhCompileFn compile = nullptr;
    WhFreeFn freeMem = nullptr;
};

Warehouse g_wh;
std::once_flag g_once;
bool g_loaded = false;

void LoadWarehouse() {
    std::call_once(g_once, []() {
        // 优先按模块自身目录找，这样游戏包被拷到别处也不用改配
        char self[MAX_PATH] = {};
        HMODULE self_ = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)&LoadWarehouse, &self_);
        if (self_) GetModuleFileNameA(self_, self, MAX_PATH);
        std::string dir;
        if (self[0]) {
            std::string p = self;
            size_t sl = p.find_last_of("\\/");
            if (sl != std::string::npos) dir = p.substr(0, sl + 1);
        }

        const std::string names[] = {
            dir + "Gvtcier_Data\\warehouse.dll",
            dir + "warehouse.dll",
            "warehouse.dll",
        };
        for (const std::string& n : names) {
            g_wh.module = LoadLibraryA(n.c_str());
            if (g_wh.module) break;
        }
        if (!g_wh.module) return;

        HMODULE m = g_wh.module;
        g_wh.init = (WhInitFn)GetProcAddress(m, "wh_init");
        g_wh.compile = (WhCompileFn)GetProcAddress(m, "wh_compile_glsl");
        g_wh.freeMem = (WhFreeFn)GetProcAddress(m, "wh_free");

        g_loaded = g_wh.init && g_wh.compile && g_wh.freeMem;
        if (g_loaded && g_wh.init() != 1) g_loaded = false;
    });
}

int ToStage(unsigned stage) {
    switch (stage) {
        case VertexShader:   return 0;
        case GeometryShader: return 3;
        case FragmentShader: return 4;
        case ComputeShader:  return 5;
        default:             return 0;
    }
}

}

bool GlslToSpirv(unsigned stage, const std::string& source, std::vector<uint32_t>& out, std::string& log) {
    LoadWarehouse();
    if (!g_loaded) {
        log = "找不到 warehouse.dll，或者它初始化失败";
        return false;
    }

    unsigned int* words = nullptr;
    int count = 0;
    const char* msg = "";
    int ok = g_wh.compile(ToStage(stage), source.c_str(), (int)source.size(), &words, &count, &msg);
    if (!ok || !words || count <= 0) {
        log = (msg && msg[0]) ? msg : "GLSL 编译失败";
        if (words) g_wh.freeMem(words);
        return false;
    }

    out.assign(words, words + count);
    g_wh.freeMem(words);
    if (msg && msg[0]) log = msg;
    return true;
}

}
}
