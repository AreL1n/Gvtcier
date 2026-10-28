#include <android/asset_manager.h>
#include <cctype>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include "Gvtcier.h"

//Android 侧的 res_get 实现：从 APK 的 assets 或资源包中按名字取着色器等内容。
//与 Windows 版的区别在于资源可能散落在 assets 目录（大小写敏感）或被打进 gpk，因此要依次尝试多种名字写法。
//全局暂存的 AssetManager，由安卓入口在启动时注入。
static AAssetManager* g_resAssetMgr = nullptr;

//注入 AssetManager。必须是进程级有效的实例，引擎不负责释放。
void Gvtcier_Android_SetAssetManager(AAssetManager* mgr) {
    g_resAssetMgr = mgr;
}

namespace {

//res_get 可能被多线程调用（渲染线程与加载线程），缓存需要互斥保护。
std::mutex g_mutex;

//名字转大写。资源表里的条目名统一为大写，查找前必须转换；长度上限 64 防止异常长名字拖慢查找。
std::string upper(const char* name) {
    std::string up;
    for (size_t i = 0; name[i] && i < 64; ++i)
        up += (char)toupper((unsigned char)name[i]);
    return up;
}

//直接从 APK 的 assets 读一个文件到 out。读的是打包进 APK 的原始资源，不经资源包。
//AASSET_MODE_BUFFER 让系统一次性缓冲整个文件，适合着色器这类小文件。
bool readAsset(const std::string& rel, std::vector<unsigned char>& out) {
    if (!g_resAssetMgr) return false;
    AAsset* a = AAssetManager_open(g_resAssetMgr, rel.c_str(), AASSET_MODE_BUFFER);
    if (!a) return false;
    size_t n = (size_t)AAsset_getLength(a);
    out.resize(n);
    if (n > 0) AAsset_read(a, out.data(), n);
    AAsset_close(a);
    return true;
}

//四级查找：资源包 → 磁盘文件 → assets 根 → assets 的 gvres/ 子目录。
//与桌面端的"模组→主包→磁盘"不同，安卓多出 assets 两级：APK 里的资源可能放在根，也可能统一放在 gvres/ 下，
//两种布局都要兼容，否则同样的资源换一种打包方式就找不到。
//空文件视为未找到，避免把 0 字节结果当成有效负载。
bool tryRead(const std::string& rel, std::vector<unsigned char>& out) {
    if (Gvtcier_Res_HasPack() && Gvtcier_Res_Read(rel.c_str(), out) && !out.empty()) return true;
    if (Gvtcier_File_Gopen(rel.c_str(), out) && !out.empty()) return true;
    if (readAsset(rel, out) && !out.empty()) return true;
    std::string pref = "gvres/";
    pref += rel;
    if (readAsset(pref, out) && !out.empty()) return true;
    return false;
}

//名字转小写。assets 目录区分大小写，而资源表里存的是小写文件名，查找前需要统一。
std::string lower(const char* name) {
    std::string s;
    for (size_t i = 0; name[i] && i < 128; ++i)
        s += (char)tolower((unsigned char)name[i]);
    return s;
}

//把资源 id 的最后一段下划线换成点号。
//资源表里的 id 形如 "vein_1_18"，而实际文件名是 "vein_1.18.png" 这类带点的形式，
//下划线在 id 里被用作分隔符，转换后才能拼出真正的文件名。
//没有下划线时返回空串，表示无法转换。
std::string keyToDot(const std::string& s) {
    size_t u = s.find_last_of('_');
    if (u == std::string::npos || u + 1 >= s.size()) return std::string();
    std::string r = s;
    r[u] = '.';
    return r;
}

//按一组候选写法依次尝试读取：原样 → 全小写 → 下划线转点 → 小写加 .otf → 原名加 .otf。
//后两种针对字体资源：安卓侧字体的文件名可能不带扩展名，或统一以 .otf 结尾。
//全部失败时清空 out 并返回 false，保证调用方不会拿到上一次的残留内容。
bool readAny(const char* name, std::vector<unsigned char>& out) {
    std::string n = name;
    std::string lo = lower(name);
    std::vector<std::string> cands;
    cands.push_back(n);
    if (lo != n) cands.push_back(lo);
    std::string nd = keyToDot(lo);
    if (!nd.empty()) cands.push_back(nd);
    cands.push_back(lo + ".otf");
    cands.push_back(n + ".otf");
    for (size_t i = 0; i < cands.size(); ++i) {
        if (cands[i].empty()) continue;
        if (tryRead(cands[i], out)) return true;
    }
    out.clear();
    return false;
}

}

//安卓侧的资源读取入口（与 Windows 版同名同签名，供着色器加载等代码共用）。
//返回的指针指向内部缓存，长度通过 outSize 返回；缓存常驻不释放，因此只要不反复读同名资源，内存占用有界。
//返回的生命周期与进程一致，调用方不得释放；未找到时返回空指针且 outSize 置 0。
extern "C" const void* res_get(const char* name, int* outSize) {
    if (outSize) *outSize = 0;
    if (!name || !name[0]) return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    static std::map<std::string, std::vector<unsigned char>> cache;
    std::string key = upper(name);
    auto it = cache.find(key);
    if (it == cache.end()) {
        std::vector<unsigned char> data;
        if (!readAny(name, data)) return nullptr;
        it = cache.emplace(key, std::move(data)).first;
    }
    if (outSize) *outSize = (int)it->second.size();
    return it->second.data();
}
