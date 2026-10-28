#include "Gvtcier.h"
#include <cstring>
#include <string>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
//主资源包读取器（单例）。整个运行期只挂一个主包，切换包时先 close 再 open。
GpakReader& packReader() {
    static GpakReader r;
    return r;
}

//一个模组层：独立的 .glc 包读取器加上它自己的路径（路径仅用于日志与排查）。
struct ModLayer {
    GlcReader reader;
    std::string path;
};

//模组层列表，按加载顺序排列。查找时从后往前遍历，因此后加载的模组优先级更高。
std::vector<ModLayer>& modLayers() {
    static std::vector<ModLayer> v;
    return v;
}

//主包是否已成功打开。所有走主包的查询都要先看它，避免对未初始化的读取器反复调用。
bool loaded = false;
//内嵌资源包的字节快照。DLL 提供的资源指针属于 DLL 模块内存，单独拷一份能让释放时机不受模块影响。
std::vector<unsigned char> packBytes;

//从磁盘直接读文件到缓冲区。
bool readDisk(const char* path, std::vector<unsigned char>& out) {
    return Gvtcier_File_Gopen(path, out);
}

//去掉路径里的目录部分，只留文件名，用于"按文件名兜底查找"。
//包内条目全按扁平名字存放，而调用方常带目录（如 "snd/1_1.wav"），所以两种写法都要试。
std::string stripDirs(const char* path) {
    if (!path) return std::string();
    const char* p = path;
    for (const char* q = path; *q; ++q)
        if (*q == '/' || *q == '\\') p = q + 1;
    return std::string(p);
}

//主包层：是否含该路径。先按完整路径查，再用纯文件名兜底。
bool mainHas(const std::string& path) {
    if (!loaded) return false;
    if (packReader().has(path.c_str())) return true;
    std::string base = stripDirs(path.c_str());
    return base != path && packReader().has(base.c_str());
}

//主包层：解出内容。与 mainHas 同样的两级查找顺序。
bool mainExtract(const std::string& path, std::vector<unsigned char>& out) {
    if (!loaded) return false;
    if (packReader().extract(path.c_str(), out)) return true;
    std::string base = stripDirs(path.c_str());
    if (base != path && packReader().extract(base.c_str(), out)) return true;
    return false;
}

//模组层：解出内容。倒序遍历模组列表，从最后一个加载的模组开始找，实现"后加载覆盖先加载"。
bool modExtract(const std::string& path, std::vector<unsigned char>& out) {
    std::string base = stripDirs(path.c_str());
    std::vector<ModLayer>& layers = modLayers();
    for (size_t i = layers.size(); i-- > 0;) {
        if (layers[i].reader.extract(path.c_str(), out)) return true;
        if (base != path && layers[i].reader.extract(base.c_str(), out)) return true;
    }
    return false;
}

//模组层：是否含该路径，遍历顺序与 modExtract 保持一致。
bool modHas(const std::string& path) {
    std::string base = stripDirs(path.c_str());
    std::vector<ModLayer>& layers = modLayers();
    for (size_t i = layers.size(); i-- > 0;) {
        if (layers[i].reader.has(path.c_str())) return true;
        if (base != path && layers[i].reader.has(base.c_str())) return true;
    }
    return false;
}
}

//挂载内存中的资源包（游戏 DLL 内嵌 gpak 走这条路）。
//复制一份字节后解析；解析失败会清空缓冲并把 loaded 置假，随后所有主包层查询都返回未命中，自然回落到磁盘。
//size < 20 是最小包头长度（魔数 4 + 版本 4 + 条目数 4 + 至少一个条目头），比它还小必定不是合法包。
void Gvtcier_Res_SetPack(const void* data, uint64_t size) {
    Gvtcier_Res_ClearPack();
    if (!data || size < 20) return;
    packBytes.assign((const unsigned char*)data, (const unsigned char*)data + size);
    loaded = packReader().openBytes(packBytes.data(), packBytes.size());
    if (!loaded) {
        packBytes.clear();
        printf("[资源] 资源包解析失败\n");
        rlog::Gvtcier_Log_Write("[资源] 资源包解析失败\n");
    }
}

//挂载磁盘上的资源包（外部 .gpk）。失败只记日志：没有资源包时资源仍可从磁盘目录读取。
void Gvtcier_Res_SetPackFile(const char* path) {
    Gvtcier_Res_ClearPack();
    if (!path || !path[0]) return;
    loaded = packReader().open(path);
    if (!loaded) {
        printf("[资源] 资源包打开失败: %s\n", path);
        rlog::Gvtcier_Log_Write("[资源] 资源包打开失败: %s\n", path);
    }
}

//与 SetPackFile 相同，但把成败返回给调用方。需要据此决定是否中止启动的场合（如 .gpk 模式）用这个。
bool Gvtcier_Res_SetPackFileOk(const char* path) {
    Gvtcier_Res_ClearPack();
    if (!path || !path[0]) return false;
    loaded = packReader().open(path);
    if (!loaded) {
        printf("[资源] 资源包打开失败: %s\n", path);
        rlog::Gvtcier_Log_Write("[资源] 资源包打开失败: %s\n", path);
    }
    return loaded;
}

//卸下主包：关闭读取器并释放字节快照。切换游戏包时必须先调用，否则旧数据会与新包混在一起。
void Gvtcier_Res_ClearPack() {
    packReader().close();
    packBytes.clear();
    loaded = false;
}

//主包是否已挂载。
bool Gvtcier_Res_HasPack() {
    return loaded;
}

//挂载一个模组包（.glc）。成功后追加到模组层列表末尾，因此它的优先级高于之前加载的所有模组，但低于之后加载的。
//返回 false 表示文件打不开或格式非法，此时列表不变。
bool Gvtcier_Res_AddMod(const char* glcPath) {
    if (!glcPath || !glcPath[0]) return false;
    ModLayer layer;
    if (!layer.reader.open(glcPath)) {
        printf("[mod] 模组包打开失败: %s\n", glcPath);
        rlog::Gvtcier_Log_Write("[mod] 模组包打开失败: %s\n", glcPath);
        return false;
    }
    layer.path = glcPath;
    const GlcMeta& m = layer.reader.meta();
    printf("[mod] 已加载 '%s' 名称='%s' 版本='%s' 作者='%s' 条目=%zu\n",
           glcPath, m.name.c_str(), m.version.c_str(), m.author.c_str(),
           layer.reader.entries().size());
    rlog::Gvtcier_Log_Write("[mod] 已加载 '%s' 名称='%s' 版本='%s' 作者='%s' 条目=%zu\n",
           glcPath, m.name.c_str(), m.version.c_str(), m.author.c_str(),
           layer.reader.entries().size());
    modLayers().push_back(std::move(layer));
    return true;
}

//卸下全部模组层。
void Gvtcier_Res_ClearMods() {
    modLayers().clear();
}

//已挂载的模组数量。
int Gvtcier_Res_ModCount() {
    return (int)modLayers().size();
}

//取第 index 个模组的元信息；越界返回空指针。返回的生命周期与模组层一致。
const GlcMeta* Gvtcier_Res_ModMeta(int index) {
    std::vector<ModLayer>& layers = modLayers();
    if (index < 0 || index >= (int)layers.size()) return nullptr;
    return &layers[(size_t)index].reader.meta();
}

//列出所有模组中路径以 prefix 开头的条目，结果去重后写入 out 并返回条数。
//倒序遍历保证同名条目只保留优先级最高的那个；去重是线性比较，模组条目通常只有几百个，代价可接受。
int Gvtcier_Res_ListMod(const char* prefix, std::vector<std::string>& out) {
    out.clear();
    std::string pre = prefix ? prefix : "";
    std::vector<ModLayer>& layers = modLayers();
    for (size_t i = layers.size(); i-- > 0;) {
        const std::vector<GlcEntry>& es = layers[i].reader.entries();
        for (size_t k = 0; k < es.size(); ++k) {
            const std::string& p = es[k].path;
            if (p.size() <= pre.size()) continue;
            if (p.compare(0, pre.size(), pre) != 0) continue;
            bool dup = false;
            for (size_t j = 0; j < out.size(); ++j) {
                if (out[j] == p) { dup = true; break; }
            }
            if (!dup) out.push_back(p);
        }
    }
    return (int)out.size();
}

//两层查找：模组层 → 主包层。只用于确认资源在包体系里存在，不查磁盘。
bool Gvtcier_Res_Read(const char* logicalPath, std::vector<unsigned char>& out) {
    if (!logicalPath || !logicalPath[0]) return false;
    std::string path = logicalPath;
    if (modExtract(path, out)) return true;
    return mainExtract(path, out);
}

//存在性查询的完整三层顺序：模组层 → 主包层 → 磁盘文件。
//顺序即优先级：模组可以覆盖主包，主包可以覆盖磁盘，这样模组永远能替换游戏原资源。
bool Gvtcier_Res_Exists(const char* logicalPath) {
    if (!logicalPath || !logicalPath[0]) return false;
    std::string path = logicalPath;
    if (modHas(path)) return true;
    if (mainHas(path)) return true;
    std::string w = logicalPath;
    FILE* f = fopen(w.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

//读资源的主入口，完整三层顺序：模组层 → 主包层 → 磁盘文件。
//磁盘这一层特别支持纯文件名兜底：包内条目是扁平名，调用方给的路径可能带目录，
//两种写法都试一遍才能既兼容带目录的资源清单，也兼容裸文件名。
//全部未命中时打印并写日志，返回 false。
bool Gvtcier_Res_ReadFile(const char* assetPath, std::vector<unsigned char>& out) {
    if (!assetPath || !assetPath[0]) return false;
    std::string path = assetPath;
    if (modExtract(path, out)) return true;
    if (mainExtract(path, out)) return true;
    if (readDisk(assetPath, out)) return true;
    std::string base = stripDirs(assetPath);
    if (base != assetPath && readDisk(base.c_str(), out)) return true;
    //找不到是很常见的事：模组文件、可选贴图都会走到这里，调用方自己会判空。
    //这里不再刷一行失败，免得日志里全是这种噪音。
    return false;
}

//只读主包层与磁盘，跳过模组层。
//用于游戏身份类文件（meta.json、封面等）：这些不应被模组替换，否则模组可以伪装成另一个游戏。
//失败不打印，因为调用方通常把它当可选内容处理。
bool Gvtcier_Res_ReadMain(const char* assetPath, std::vector<unsigned char>& out) {
    if (!assetPath || !assetPath[0]) return false;
    std::string path = assetPath;
    if (mainExtract(path, out)) return true;
    if (readDisk(assetPath, out)) return true;
    std::string base = stripDirs(assetPath);
    if (base != assetPath && readDisk(base.c_str(), out)) return true;
    return false;
}
