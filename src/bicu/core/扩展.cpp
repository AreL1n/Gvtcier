#include "Gvtcier.h"
#include <windows.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

//已成功加载的扩展模块列表。持有模块句柄，卸载时统一 FreeLibrary。
static std::vector<GvtcierExpandModule> s_modules;

//扫描可执行文件目录下的 Modes 子目录，加载其中所有 .dll 扩展。
//每个扩展必须导出 Gvtcier_expand 符号，由引擎把上下文与模块槽位传进去，扩展自己完成注册。
//任何一步失败（加载失败、缺导出、注册失败、ABI 不匹配）都只跳过该扩展并立即卸载模块，不影响其它扩展，
//也不影响引擎启动：扩展属于增强能力，缺失时应降级而不是崩溃。
//ctx 为传给扩展的上下文，开始时写入引擎的 ABI 版本号，扩展可据此判断兼容性。
//副作用：先卸载此前加载的全部扩展（重复调用不会叠加），成功后填充 s_modules。
void Gvtcier_Expand_LoadAll(GvtcierExpandCtx& ctx) {
    if (!s_modules.empty()) Gvtcier_Expand_UnloadAll();
    ctx.abiVersion = GVTCIER_EXPAND_ABI;
    std::string root = Gvtcier_W32_ModuleDirUtf8();
    if (root.empty()) {
        DBGLN("[expand] cannot resolve module dir");
        return;
    }

    std::string modesDir = root + "\\Modes";
    if (!Gvtcier_W32_FileExistsUtf8(modesDir.c_str())) {
        DBGLN("[expand] no Modes dir");
        return;
    }

    std::string search = modesDir + "\\*.dll";
    std::vector<std::string> files = Gvtcier_W32_FindFilesUtf8(search.c_str());
    if (files.empty()) {
        DBGLN("[expand] no dll in Modes");
        return;
    }

    for (size_t i = 0; i < files.size(); ++i) {
        const std::string& name = files[i];
        std::string full = modesDir + "\\" + name;
        HMODULE mod = (HMODULE)Gvtcier_W32_LoadLibraryUtf8(full.c_str());
        if (!mod) {
            DBGLN("[expand] load fail %s", name.c_str());
            continue;
        }
        GvtcierExpandEntry entry = (GvtcierExpandEntry)GetProcAddress(mod, "Gvtcier_expand");
        if (!entry) {
            //不是扩展 DLL：直接卸载，避免误加载的其它库常驻内存。
            DBGLN("[expand] no Gvtcier_expand %s", name.c_str());
            FreeLibrary(mod);
            continue;
        }
        GvtcierExpandModule m;
        m.handle = (void*)mod;
        strncpy(m.name, name.c_str(), sizeof(m.name) - 1);
        m.name[sizeof(m.name) - 1] = 0;
        if (!entry(&ctx, &m)) {
            DBGLN("[expand] register fail %s", name.c_str());
            FreeLibrary(mod);
            continue;
        }
        if (m.abiVersion != GVTCIER_EXPAND_ABI) {
            //扩展自己填回的 ABI 号与引擎不一致，说明它用旧头文件编译，结构体布局可能已变，直接拒绝加载。
            DBGLN("[expand] ABI mismatch %s (module=%d engine=%d) 请重新编译模块",
                  name.c_str(), m.abiVersion, GVTCIER_EXPAND_ABI);
            FreeLibrary(mod);
            continue;
        }
        s_modules.push_back(m);
        DBGLN("[expand] loaded %s", name.c_str());
    }
}

//卸载全部扩展：逐个释放模块句柄并清空列表。
//注意这里只释放 DLL，不通知扩展做清理，因此扩展不应把需要显式收尾的资源留到卸载阶段。
void Gvtcier_Expand_UnloadAll() {
    for (size_t i = 0; i < s_modules.size(); ++i)
        if (s_modules[i].handle) FreeLibrary((HMODULE)s_modules[i].handle);
    s_modules.clear();
}
