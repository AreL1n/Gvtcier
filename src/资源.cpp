#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

//Windows 侧的资源读取实现：从 PE 文件的 RT_RCDATA 资源段里按名字取数据。
//着色器源文本、字体、图标等都通过 res.rc 打进可执行文件，运行时按资源名查找，无需外部文件。

//在指定模块的资源段里按名字查找。找到返回资源内存指针，长度写入 outSize。
//返回值指向模块内存的只读视图，不需要也不能释放；模块卸载前一直有效。
//名字必须是大写，这是 Windows 资源查找的规则。
static const void* resFind(HMODULE mod, const char* up, int* outSize) {
    if (!mod) return nullptr;
    HRSRC hr = FindResourceA(mod, up, RT_RCDATA);
    if (!hr) return nullptr;
    HGLOBAL hg = LoadResource(mod, hr);
    if (!hg) return nullptr;
    void* p = LockResource(hg);
    if (!p) return nullptr;
    if (outSize) *outSize = (int)SizeofResource(mod, hr);
    return p;
}

//取本文件所属模块（即 Gvtcier.dll）的句柄。
//用"按函数地址反查模块"而不是 GetModuleHandle("Gvtcier.dll")：前者不受文件名改动或加载方式影响，更可靠。
//UNCHANGED_REFCOUNT 表示不增加引用计数，避免句柄泄漏。
static HMODULE resSelfModule() {
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&resFind, &mod);
    return mod;
}

//两级查找：先在主程序 exe 里找，再在本 DLL 里找。
//先查 exe 是为了允许外部工具或游戏包覆盖引擎自带资源；查不到再回落到引擎自身的资源段。
static const void* resTry(const char* up, int* outSize) {
    const void* p = resFind(GetModuleHandleA(NULL), up, outSize);
    if (p) return p;
    return resFind(resSelfModule(), up, outSize);
}

//按名字取资源（名字不区分大小写，内部统一转大写）。未找到返回空指针，outSize 置 0。
//名字长度上限 63 字节：资源名都很短，超长名字直接判为无效，避免缓冲区溢出。
extern "C" __declspec(dllexport) const void* res_get(const char* name, int* outSize) {
    if (outSize) *outSize = 0;
    if (!name || !name[0]) return nullptr;
    char up[64];
    size_t i = 0;
    for (; name[i] && i < sizeof(up) - 1; ++i)
        up[i] = (char)toupper((unsigned char)name[i]);
    up[i] = '\0';
    if (i >= sizeof(up) - 1 && name[i]) return nullptr;
    return resTry(up, outSize);
}

//按数字 id 取着色器资源，资源名形如 "SHADER_<id>"。
//着色器用编号而不是文件名引用，因为编号由构建脚本统一生成，改名不会破坏引用。
//id 为负视为无效。
extern "C" __declspec(dllexport) const void* res_get_shader(int id, int* outSize) {
    if (outSize) *outSize = 0;
    if (id < 0) return nullptr;
    char key[32];
    snprintf(key, sizeof(key), "SHADER_%d", id);
    return resTry(key, outSize);
}
