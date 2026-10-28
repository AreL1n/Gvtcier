#include "Gvtcier.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <map>
#include <string>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// 顶点数据的收尾处理：先归一化已有法线，再用“光滑法线”覆盖。
// 顺序上先归一化是为 buildSmoothNormals 准备合法的输入（长度不为零的法线）；
// 注意这里的光滑法线会在 OBJ 已显式提供法线的情况下也覆盖掉它们
void finalizeVerts(std::vector<Vertex>& verts) {
    GvtcierNormal::normalizeVertNormals(verts);
    buildSmoothNormals(verts);
}

// 解析 OBJ 文本并创建 GPU 网格。OBJ 格式要点：
//   v x y z      顶点位置
//   vn x y z     顶点法线，可选
//   vt u v       纹理坐标，可选
//   f a/b/c ...  面，每个角写作 顶点/纹理/法线 的索引（三者都可能省略），
//                索引从 1 开始，负数表示“从末尾往前数”
// 解析策略：不做顶点索引去重，面里每个角都新建一个顶点，因此 UV 接缝处的
// 顶点自然分离；每个面都会被三角化成 (cornerCount - 2) 个三角形（扇形三角化，
// 只对凸多边形正确，凹多边形会出现越界三角）。
// 返回 false 表示没有任何有效面或 GPU 网格创建失败
bool loadObjMesh(const void* data, size_t size, Mesh& out) {
    const char* text = (const char*)data;
    std::vector<Vec3> pos, nor;
    std::vector<Vec2> uv;
    std::vector<Vertex> verts;
    std::vector<unsigned> indices;

    // 逐行扫描：i 指向行首，先推进到行尾换行符，再在 [p, e) 区间内解析。
    // 用原始指针而不是 std::string 拆分，是为了避免大数据量下的临时字符串分配
    size_t i = 0;
    while (i < size) {
        const char* ls = text + i;
        while (i < size && text[i] != '\n') ++i;
        const char* p = ls;
        const char* e = text + i;
        ++i;
        // 跳过行首空白与 \r（Windows 换行），整行注释直接丢弃
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        if (p >= e || *p == '#') continue;
        if (e - p >= 2 && p[0] == 'v' && p[1] == ' ') {
            Vec3 v;
            if (sscanf(p + 2, "%lf %lf %lf", &v.x, &v.y, &v.z) == 3) pos.push_back(v);
        } else if (e - p >= 3 && p[0] == 'v' && p[1] == 'n' && (p[2] == ' ' || p[2] == '\t')) {
            Vec3 v;
            if (sscanf(p + 3, "%lf %lf %lf", &v.x, &v.y, &v.z) == 3) nor.push_back(v);
        } else if (e - p >= 3 && p[0] == 'v' && p[1] == 't' && (p[2] == ' ' || p[2] == '\t')) {
            Vec2 v;
            if (sscanf(p + 3, "%lf %lf", &v.x, &v.y) == 2) uv.push_back(v);
        } else if (p[0] == 'f' && (p[1] == ' ' || p[1] == '\t')) {
            const char* q = p + 2;
            // 单个面最多支持 64 个角，超过部分被截断（多边形面通常会先被三角化导出）
            unsigned face[64];
            int cornerCount = 0;
            Vec3 fcNor[64];
            int fcNorSet[64];
            while (q < e && cornerCount < 64) {
                while (q < e && (*q == ' ' || *q == '\t')) ++q;
                if (q >= e || *q == '\r') break;
                int vi = 0, ti = 0, ni = 0;
                char* ep = nullptr;
                vi = (int)strtol(q, &ep, 10);
                if (ep == q) break;
                q = ep;
                // 斜杠语法：a、a/b、a//c、a/b/c 四种写法都要支持，
                // “//” 意味着没有纹理坐标但有法线
                if (q < e && *q == '/') {
                    ++q;
                    if (q < e && *q != '/') { ti = (int)strtol(q, &ep, 10); q = ep; }
                    if (q < e && *q == '/') {
                        ++q;
                        ni = (int)strtol(q, &ep, 10);
                        q = ep;
                    }
                }
                // OBJ 索引是 1 基；负数按“从尾部倒数”解释（0 与缺省都当作无效）
                int pi = vi > 0 ? vi - 1 : (int)pos.size() + vi;
                int pn = ni > 0 ? ni - 1 : (int)nor.size() + ni;
                int pt = ti > 0 ? ti - 1 : (int)uv.size() + ti;
                Vertex v;
                // 索引越界时用零值兜底而不是报错，尽量把模型加载出来
                v.pos = (pi >= 0 && pi < (int)pos.size()) ? pos[pi] : Vec3(0, 0, 0);
                v.normal = Vec3(0, 0, 0);
                v.uv = (pt >= 0 && pt < (int)uv.size()) ? uv[pt] : Vec2(0, 0);
                face[cornerCount] = (unsigned)verts.size();
                fcNor[cornerCount] = (pn >= 0 && pn < (int)nor.size()) ? nor[pn] : Vec3(0, 0, 0);
                fcNorSet[cornerCount] = (pn >= 0 && pn < (int)nor.size()) ? 1 : 0;
                verts.push_back(v);
                ++cornerCount;
            }
            if (cornerCount < 3) continue;
            // 扇形三角化：以第 0 个角为公共顶点，连接相邻两个角
            for (int t = 1; t + 1 < cornerCount; ++t) {
                unsigned a = face[0], b = face[t], c = face[t + 1];
                if (fcNorSet[0] && fcNorSet[t] && fcNorSet[t + 1]) {
                    verts[a].normal = fcNor[0];
                    verts[b].normal = fcNor[t];
                    verts[c].normal = fcNor[t + 1];
                } else {
                    // 缺少法线时用叉积求面法线；叉积长度为 0（退化三角形）时退回 Y 轴，
                    // 避免产生 NaN 法线污染后续计算
                    Vec3 fn = (verts[b].pos - verts[a].pos).cross(verts[c].pos - verts[a].pos);
                    float len = fn.length();
                    fn = len > 1e-9f ? fn * (1.0f / len) : Vec3(0, 1, 0);
                    if (!fcNorSet[0]) verts[a].normal = fn;
                    if (!fcNorSet[t]) verts[b].normal = fn;
                    if (!fcNorSet[t + 1]) verts[c].normal = fn;
                }
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(c);
            }
        }
    }
    if (verts.empty() || indices.empty()) {
        printf("[asset] OBJ: no faces found\n");
        return false;
    }
    // 完全没有 UV 的模型（如雕塑、程序化导出）用位置做平面投影，避免整张贴图采到同一点
    bool anyUv = false;
    for (size_t k = 0; k < verts.size(); ++k)
        if (verts[k].uv.x != 0.0f || verts[k].uv.y != 0.0f) { anyUv = true; break; }
    if (!anyUv) {
        for (size_t k = 0; k < verts.size(); ++k)
            verts[k].uv = Vec2(verts[k].pos.x * 0.5f + 0.5f, verts[k].pos.y * 0.5f + 0.5f);
    }
    // 统一绕序：引擎约定三角形按逆时针（CCW）为正面，OBJ 导出方可能用顺时针，
    // 检测到整体反绕后翻转索引顺序并同步取反法线，否则光照会内外颠倒
    if (GvtcierNormal::needsWindingFlip(verts, indices)) {
        GvtcierNormal::flipWinding(indices);
        GvtcierNormal::flipNormals(verts);
        printf("[asset] OBJ: 绕序反转为 CCW\n");
    }
    // 普通静态网格不需要骨骼权重，清零第 0 组权重保证顶点布局完整
    for (size_t k = 0; k < verts.size(); ++k) {
        verts[k].joint[0] = 0;
        verts[k].weight[0] = 0.0f;
    }
    finalizeVerts(verts);
    return out.create(verts.data(), (int)verts.size(), indices.data(), (int)indices.size());
}

// 屏蔽 tinygltf 自带的图像解码与外部图片读取，改由本文件的 stb 解码器与内存数据提供，
// 这样整个加载过程不需要文件系统，可直接从打包资源的内存块解析
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_IMPLEMENTATION
#include "tiny_gltf.h"

// 从内存字节解码图片为 RGBA8。返回 false 表示数据无效、解码失败或尺寸超限（8192）
static bool gdiDecodeImage(const unsigned char* bytes, int size,
                           std::vector<unsigned char>& outRgba, int& outW, int& outH) {
    if (!bytes || size <= 0) return false;
    int w = 0, h = 0, comp = 0;
    // 与场景贴图一致地翻转 Y，保证 GLTF 的 UV 与引擎采样方向匹配
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        if (data) stbi_image_free(data);
        return false;
    }
    outRgba.assign(data, data + (size_t)w * h * 4);
    stbi_image_free(data);
    outW = w;
    outH = h;
    return true;
}

// tinygltf 的图像加载回调。解码失败时不清空整个模型，而是写回一张空图并返回 true，
// 让模型几何仍能加载，只是没有贴图
static bool gvtLoadImageData(tinygltf::Image* image, const int image_idx, std::string* err,
                             std::string* warn, int req_width, int req_height,
                             const unsigned char* bytes, int size, void* user_data) {
    (void)image_idx; (void)err; (void)warn; (void)req_width; (void)req_height; (void)user_data;
    if (!image) return false;
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!gdiDecodeImage(bytes, size, rgba, w, h)) {
        image->image.clear();
        image->width = 0;
        image->height = 0;
        image->component = 0;
        image->bits = 0;
        return true;
    }
    image->image.assign((const unsigned char*)rgba.data(),
                        (const unsigned char*)rgba.data() + rgba.size());
    image->width = w;
    image->height = h;
    image->component = 4;
    image->bits = 8;
    return true;
}

// 解析 GLTF：先按二进制（.glb）尝试，失败且不以 "glTF" 魔数开头时再按文本 JSON（.gltf）解析。
// 以魔数开头却解析失败说明文件已损坏，直接返回失败而不是再试文本模式
static bool loadTinyModel(const void* data, size_t size, tinygltf::Model& model) {
    tinygltf::TinyGLTF ctx;
    ctx.SetImageLoader(gvtLoadImageData, nullptr);
    std::string err, warn;
    bool ok = ctx.LoadBinaryFromMemory(&model, &err, &warn,
                                       (const unsigned char*)data, (unsigned int)size);
    if (!ok) {
        if (size >= 4 && memcmp(data, "glTF", 4) == 0) return false;
        ok = ctx.LoadASCIIFromString(&model, &err, &warn,
                                     (const char*)data, (unsigned int)size, "");
    }
    return ok;
}

// accessor 类型到分量个数的映射。MAT4 返回 16，用于骨骼逆绑定矩阵读取
static int accComps(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        case TINYGLTF_TYPE_MAT4: return 16;
        default: return 0;
    }
}

// 把 accessor 的内容统一读成 float 数组（不区分原始分量类型，全部转换提升为 float）。
// GLTF 的 accessor 通过 bufferView 定位到 buffer 里的字节区间：
//   stride 优先用 bufferView.byteStride（交错顶点缓冲），否则按 分量数*分量字节数 紧排；
//   实际数据起点 = bv.byteOffset + a.byteOffset。
// 越界检查用 “起点 + count*stride” 与 buffer 总字节数比较，防止恶意文件读越界。
// 返回 false 表示索引非法、类型不支持或数据越界
static bool accFloat(const tinygltf::Model& m, int accIdx, std::vector<float>& out, int& comps) {
    if (accIdx < 0 || accIdx >= (int)m.accessors.size()) return false;
    const tinygltf::Accessor& a = m.accessors[accIdx];
    comps = accComps(a.type);
    int csz = tinygltf::GetComponentSizeInBytes((uint32_t)a.componentType);
    if (comps <= 0 || csz <= 0 || a.bufferView < 0) return false;
    const tinygltf::BufferView& bv = m.bufferViews[a.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)m.buffers.size()) return false;
    const tinygltf::Buffer& b = m.buffers[bv.buffer];
    size_t stride = bv.byteStride ? (size_t)bv.byteStride : (size_t)comps * (size_t)csz;
    const unsigned char* src = b.data.data() + bv.byteOffset + a.byteOffset;
    if (bv.byteOffset + a.byteOffset + (size_t)a.count * stride > b.data.size()) return false;
    out.resize((size_t)a.count * comps);
    for (int i = 0; i < (int)a.count; ++i) {
        const unsigned char* p = src + (size_t)i * stride;
        for (int c = 0; c < comps; ++c) {
            float v = 0.0f;
            // 逐分量按原始类型取值。这里的偏移按分量字节宽度累加（c*2/c*4），
            // 依赖 stride 至少等于 comps*csz，否则交错数据会被误读
            switch (a.componentType) {
                case TINYGLTF_COMPONENT_TYPE_FLOAT: v = *(const float*)(p + c * 4); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: v = (float)(*(const unsigned short*)(p + c * 2)); break;
                case TINYGLTF_COMPONENT_TYPE_SHORT: v = (float)(*(const short*)(p + c * 2)); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: v = (float)p[c]; break;
                case TINYGLTF_COMPONENT_TYPE_BYTE: v = (float)(*(const char*)(p + c)); break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: v = (float)(*(const unsigned*)(p + c * 4)); break;
                default: return false;
            }
            out[(size_t)i * comps + c] = v;
        }
    }
    return true;
}

// 加载静态（非蒙皮）GLTF 网格。只取第 0 个 mesh 的第 0 个 primitive，
// 因此多材质/多图元的模型只会加载第一块，其余图元被忽略。
// 属性处理：
//   POSITION 必需（VEC3）；
//   NORMAL 可选，存在则归一化使用，缺失时后面用面法线累积计算；
//   TEXCOORD_0 可选，V 坐标取 1-v（GLTF 的 UV 原点在左上，引擎在左下）；
//   indices 可选，缺失时按顶点顺序作为三角形列表（等价于非索引网格）。
// 返回 false 表示结构缺失、属性数量不匹配或 GPU 创建失败
bool loadGltfMesh(const void* data, size_t size, Mesh& out) {
    tinygltf::Model model;
    if (!loadTinyModel(data, size, model)) return false;
    if (model.meshes.empty() || model.meshes[0].primitives.empty()) return false;
    const tinygltf::Primitive& prim = model.meshes[0].primitives[0];
    auto itPos = prim.attributes.find("POSITION");
    if (itPos == prim.attributes.end()) return false;

    std::vector<float> p, n, t, idx;
    int pc = 0, nc = 0, tc = 0, ic = 0;
    // POSITION 必须是三分量，否则无法据此推断顶点数
    if (!accFloat(model, itPos->second, p, pc) || pc != 3) return false;
    auto itNor = prim.attributes.find("NORMAL");
    if (itNor != prim.attributes.end() && !accFloat(model, itNor->second, n, nc)) return false;
    auto itUv = prim.attributes.find("TEXCOORD_0");
    if (itUv != prim.attributes.end() && !accFloat(model, itUv->second, t, tc)) return false;
    if (prim.indices >= 0 && !accFloat(model, prim.indices, idx, ic)) return false;

    int vcount = (int)(p.size() / 3);
    if (vcount == 0) return false;
    std::vector<Vertex> verts((size_t)vcount);
    for (int i = 0; i < vcount; ++i) {
        verts[i].pos = Vec3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
        // 只有法线数量恰好等于 顶点数*3 时才认为有效，否则视为缺失
        verts[i].normal = (n.size() == (size_t)vcount * 3)
                              ? Vec3(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]).normalized()
                              : Vec3(0, 0, 0);
        if (t.size() == (size_t)vcount * 2)
            verts[i].uv = Vec2(t[i * 2], 1.0f - t[i * 2 + 1]);
    }
    std::vector<unsigned> indices;
    if (idx.size() > 0) {
        indices.reserve(idx.size());
        for (size_t i = 0; i < idx.size(); ++i) indices.push_back((unsigned)idx[i]);
    } else {
        for (int i = 0; i < vcount; ++i) indices.push_back((unsigned)i);
    }
    if (indices.empty()) return false;
    // 绕序统一：有法线时一并翻转法线，没有法线时留给后面的面法线重算
    if (GvtcierNormal::needsWindingFlip(verts, indices)) {
        GvtcierNormal::flipWinding(indices);
        if (n.size() == (size_t)vcount * 3) GvtcierNormal::flipNormals(verts);
    }
    if (n.size() != (size_t)vcount * 3) {
        GvtcierNormal::accumulateFaceNormals(verts, indices);
        GvtcierNormal::normalizeVertNormals(verts);
    }
    finalizeVerts(verts);
    return out.create(verts.data(), vcount, indices.data(), (int)indices.size());
}

// 加载带骨骼蒙皮的 GLTF 模型，产出几何、骨骼层级、逆绑定矩阵、基础色贴图与动画轨。
// 全流程：
// 1) 找第一个同时带 mesh 与 skin 的节点，作为蒙皮模型的宿主节点；
// 2) 遍历该 mesh 的所有 primitive，合并顶点（用 vbase 偏移索引），
//    读取 JOINTS_0/WEIGHTS_0 并归一化权重；
// 3) 用逆绑定矩阵反推骨骼世界位置（作为骨架摆位），并按节点父子关系确定骨骼父子；
// 4) 依骨架高度推算每根骨骼的可视化胶囊（半长与半径），只用于调试绘制；
// 5) 读第一个带 baseColorTexture 的材质，把图像数据拷成 RGBA8；
// 6) 逐条读取动画 sampler，生成通道（translation/rotation/scale）。
// 限制：骨骼数上限 64（bone.parent 链数组与调试链固定按 64 个设计），只支持一个蒙皮网格。
// 返回 false 表示没有蒙皮节点、骨骼数非法或逆绑定矩阵缺失
bool loadSkinnedMesh(const void* data, size_t size, SkinnedMesh& out) {
    tinygltf::Model model;
    if (!loadTinyModel(data, size, model)) return false;
    const tinygltf::Mesh* mesh = nullptr;
    int skinIdx = -1;
    int meshNode = -1;
    // 取第一个满足条件的节点，因此多蒙皮角色的后续网格不会被加载
    for (size_t i = 0; i < model.nodes.size() && !mesh; ++i) {
        const tinygltf::Node& nd = model.nodes[i];
        if (nd.mesh >= 0 && nd.skin >= 0) {
            mesh = &model.meshes[nd.mesh];
            skinIdx = nd.skin;
            meshNode = (int)i;
        }
    }
    if (!mesh || skinIdx < 0 || skinIdx >= (int)model.skins.size()) return false;
    if (mesh->primitives.empty()) return false;
    const tinygltf::Skin& skin = model.skins[skinIdx];

    int boneCount = (int)skin.joints.size();
    // 64 是骨骼数上限：骨骼矩阵与 pose 数组都按此规模设计，超出直接拒绝
    if (boneCount <= 0 || boneCount > 64) return false;

    std::vector<Vertex> verts;
    std::vector<unsigned> indices;
    for (size_t pi = 0; pi < mesh->primitives.size(); ++pi) {
        const tinygltf::Primitive& prim = mesh->primitives[pi];
        auto itPos = prim.attributes.find("POSITION");
        if (itPos == prim.attributes.end()) continue;

        std::vector<float> p, n, t, jt, wt, idx;
        int pc = 0, nc = 0, tc = 0, jc = 0, wc = 0, ic = 0;
        if (!accFloat(model, itPos->second, p, pc) || pc != 3) continue;
        // 可选属性读取失败时清空对应数组，后续按“缺失”处理而不是整体失败
        auto itNor = prim.attributes.find("NORMAL");
        if (itNor != prim.attributes.end() && !accFloat(model, itNor->second, n, nc)) n.clear();
        auto itUv = prim.attributes.find("TEXCOORD_0");
        if (itUv != prim.attributes.end() && !accFloat(model, itUv->second, t, tc)) t.clear();
        auto itJnt = prim.attributes.find("JOINTS_0");
        if (itJnt != prim.attributes.end() && !accFloat(model, itJnt->second, jt, jc)) jt.clear();
        auto itWgt = prim.attributes.find("WEIGHTS_0");
        if (itWgt != prim.attributes.end() && !accFloat(model, itWgt->second, wt, wc)) wt.clear();
        if (prim.indices >= 0 && !accFloat(model, prim.indices, idx, ic)) idx.clear();

        int vcount = (int)(p.size() / 3);
        if (vcount == 0) continue;
        // 合并多个 primitive 时，索引要整体加上已有顶点数
        unsigned vbase = (unsigned)verts.size();
        size_t vstart = verts.size();
        verts.resize(vstart + (size_t)vcount);
        for (int i = 0; i < vcount; ++i) {
            Vertex& v = verts[vstart + (size_t)i];
            v.pos = Vec3(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
            v.normal = (n.size() == (size_t)vcount * 3)
                           ? Vec3(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]).normalized()
                           : Vec3(0, 0, 0);
            // GLTF 的 UV 原点在左上，这里翻转 V 对齐引擎
            if (t.size() == (size_t)vcount * 2)
                v.uv = Vec2(t[i * 2], 1.0f - t[i * 2 + 1]);
            // 关节索引越界时夹到 0，避免读到骨骼数组之外
            if (jt.size() == (size_t)vcount * 4) {
                for (int k = 0; k < 4; ++k) {
                    int ji = (int)jt[i * 4 + k];
                    v.joint[k] = (unsigned char)((ji >= 0 && ji < boneCount) ? ji : 0);
                }
            }
            // 权重必须归一化，否则着色器里的加权求和会整体变亮或变暗；
            // 没有权重属性时退化为整点绑定到 0 号骨骼
            if (wt.size() == (size_t)vcount * 4) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += wt[i * 4 + k];
                if (sum > 1e-6f) {
                    for (int k = 0; k < 4; ++k) v.weight[k] = wt[i * 4 + k] / sum;
                }
            } else {
                v.joint[0] = 0;
                v.weight[0] = 1.0f;
            }
        }
        if (!idx.empty()) {
            for (size_t i = 0; i < idx.size(); ++i) indices.push_back(vbase + (unsigned)idx[i]);
        } else {
            for (int i = 0; i < vcount; ++i) indices.push_back(vbase + (unsigned)i);
        }
    }
    if (verts.empty() || indices.empty()) return false;

    int vcount = (int)verts.size();
    // 蒙皮网格不做绕序翻转，直接铺一层光滑法线作为基础法线；
    // 蒙皮后法线由骨骼矩阵变换，这里的平滑法线只在静止姿态下准确
    buildSmoothNormals(verts);

    // 逆绑定矩阵：每根骨骼一个 4x4，用于把网格空间顶点变换到骨骼空间
    std::vector<float> ibm;
    int ibmComps = 0;
    if (!accFloat(model, skin.inverseBindMatrices, ibm, ibmComps) || ibmComps != 16) return false;
    if ((int)ibm.size() < boneCount * 16) return false;

    // 节点世界变换（位置 + 四元数）预计算。
    // 只累加 translation 与 rotation，scale 被忽略，骨架中带缩放的节点会导致位置偏差。
    // 用显式栈做深度优先遍历而不是递归，避免深骨骼链爆栈；
    // 同一节点被多个父引用时会被重复计算（GLTF 规范保证这是树，正常不会发生）
    int nodeCount = (int)model.nodes.size();
    std::vector<Vec3> wPos((size_t)nodeCount, Vec3(0, 0, 0));
    std::vector<Vec3> wRot((size_t)nodeCount, Vec3(0, 0, 0));
    std::vector<float> wRotW((size_t)nodeCount, 1.0f);
    // 用四元数旋转向量 v：v' = u*(2u·v) + v*(2w²-1) + u×v*(2w)，避免构造矩阵
    auto qrot = [](const Vec3& u, float w, const Vec3& v) -> Vec3 {
        return u * (2.0f * u.dot(v)) + v * (2.0f * w * w - 1.0f) + u.cross(v) * (2.0f * w);
    };
    // 四元数乘法：(a,aw) * (b,bw)
    auto qmul = [](const Vec3& a, float aw, const Vec3& b, float bw, Vec3& o, float& ow) {
        o = b * aw + a * bw + a.cross(b);
        ow = aw * bw - a.dot(b);
    };
    struct St { int node; Vec3 wp; Vec3 q; float qw; };
    std::vector<St> stack;
    for (size_t s = 0; s < model.scenes.size(); ++s) {
        for (size_t r = 0; r < model.scenes[s].nodes.size(); ++r) {
            int ri = model.scenes[s].nodes[r];
            if (ri < 0 || ri >= nodeCount) continue;
            const tinygltf::Node& nd = model.nodes[ri];
            Vec3 lt = nd.translation.size() == 3 ? Vec3((float)nd.translation[0], (float)nd.translation[1], (float)nd.translation[2]) : Vec3(0, 0, 0);
            Vec3 lq = nd.rotation.size() == 4 ? Vec3((float)nd.rotation[0], (float)nd.rotation[1], (float)nd.rotation[2]) : Vec3(0, 0, 0);
            float lw = nd.rotation.size() == 4 ? (float)nd.rotation[3] : 1.0f;
            stack.push_back({ ri, lt, lq, lw });
        }
    }
    while (!stack.empty()) {
        St s = stack.back(); stack.pop_back();
        wPos[s.node] = s.wp;
        wRot[s.node] = s.q;
        wRotW[s.node] = s.qw;
        const tinygltf::Node& nd = model.nodes[s.node];
        for (size_t c = 0; c < nd.children.size(); ++c) {
            int ci = nd.children[c];
            if (ci < 0 || ci >= nodeCount) continue;
            const tinygltf::Node& cd = model.nodes[ci];
            Vec3 lt2 = cd.translation.size() == 3 ? Vec3((float)cd.translation[0], (float)cd.translation[1], (float)cd.translation[2]) : Vec3(0, 0, 0);
            Vec3 lq2 = cd.rotation.size() == 4 ? Vec3((float)cd.rotation[0], (float)cd.rotation[1], (float)cd.rotation[2]) : Vec3(0, 0, 0);
            float lw2 = cd.rotation.size() == 4 ? (float)cd.rotation[3] : 1.0f;
            // 子世界位置 = 父位置 + 父旋转 * 子局部平移；子世界旋转 = 父旋转 * 子局部旋转
            Vec3 cwp = s.wp + qrot(s.q, s.qw, lt2);
            Vec3 cq; float cqw;
            qmul(s.q, s.qw, lq2, lw2, cq, cqw);
            stack.push_back({ ci, cwp, cq, cqw });
        }
    }

    out.bones.clear();
    out.bones.reserve((size_t)boneCount);
    for (int b = 0; b < boneCount; ++b) {
        int ji = skin.joints[b];
        if (ji < 0 || ji >= nodeCount) return false;
        SkinnedBone bn;
        {
            // 从逆绑定矩阵里拆分出旋转与平移：
            // ibm = R⁻¹ * T⁻¹，因此旋转部分取转置 Rᵀ，平移部分为 -(Rᵀ * t)，
            // 得到的是“绑定姿态下骨骼的世界位置与朝向”
            const float* m = &ibm[b * 16];
            float R[3][3], Rt[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) { R[r][c] = m[c * 4 + r]; Rt[r][c] = R[c][r]; }
            const float t[3] = { m[3], m[7], m[11] };
            Vec3 invT(
                -(Rt[0][0]*t[0] + Rt[0][1]*t[1] + Rt[0][2]*t[2]),
                -(Rt[1][0]*t[0] + Rt[1][1]*t[1] + Rt[1][2]*t[2]),
                -(Rt[2][0]*t[0] + Rt[2][1]*t[1] + Rt[2][2]*t[2]));
            // 旋转矩阵转四元数：按迹的正负选择四种分支，避免除以接近 0 的数，
            // 这是标准的 Shepperd 方法
            float tr = Rt[0][0] + Rt[1][1] + Rt[2][2];
            float qx, qy, qz, qw;
            if (tr > 0.0f) {
                float s = std::sqrt(tr + 1.0f) * 2.0f;
                qw = 0.25f * s;
                qx = (Rt[2][1] - Rt[1][2]) / s;
                qy = (Rt[0][2] - Rt[2][0]) / s;
                qz = (Rt[1][0] - Rt[0][1]) / s;
            } else if (Rt[0][0] > Rt[1][1] && Rt[0][0] > Rt[2][2]) {
                float s = std::sqrt(1.0f + Rt[0][0] - Rt[1][1] - Rt[2][2]) * 2.0f;
                qw = (Rt[2][1] - Rt[1][2]) / s;
                qx = 0.25f * s;
                qy = (Rt[0][1] + Rt[1][0]) / s;
                qz = (Rt[0][2] + Rt[2][0]) / s;
            } else if (Rt[1][1] > Rt[2][2]) {
                float s = std::sqrt(1.0f + Rt[1][1] - Rt[0][0] - Rt[2][2]) * 2.0f;
                qw = (Rt[0][2] - Rt[2][0]) / s;
                qx = (Rt[0][1] + Rt[1][0]) / s;
                qy = 0.25f * s;
                qz = (Rt[1][2] + Rt[2][1]) / s;
            } else {
                float s = std::sqrt(1.0f + Rt[2][2] - Rt[0][0] - Rt[1][1]) * 2.0f;
                qw = (Rt[1][0] - Rt[0][1]) / s;
                qx = (Rt[0][2] + Rt[2][0]) / s;
                qy = (Rt[1][2] + Rt[2][1]) / s;
                qz = 0.25f * s;
            }
            bn.pos = invT;
            // 逆绑定矩阵解出的平移为 0（常见于根骨骼，绑定矩阵是单位阵）时，
            // 改用节点自己的世界位置，保证根骨骼落在合理的位置上
            if (invT.dot(invT) < 1e-8f && wPos[(size_t)ji].dot(wPos[(size_t)ji]) > 1e-8f)
                bn.pos = wPos[(size_t)ji];
            bn.rotQ = Vec4(qx, qy, qz, qw);
            // dir 是骨骼朝向：把局部 Y 轴按骨骼旋转转过去。
            // 注意后面会根据子骨骼位置重新覆盖它
            Vec3 up = qrot(Vec3(qx, qy, qz), qw, Vec3(0, 1, 0));
            bn.dir = up.length() > 1e-6f ? up.normalized() : Vec3(0, 1, 0);
        }
        // 父骨骼查找：先在全部节点里找“谁把当前节点列为子节点”，
        // 再把该节点编号映射回骨骼下标；父节点不是骨骼时 parent 变成 -1（即当作根骨骼）
        bn.parent = -1;
        for (int k = 0; k < nodeCount && bn.parent < 0; ++k) {
            const tinygltf::Node& nd = model.nodes[k];
            for (size_t c = 0; c < nd.children.size(); ++c)
                if (nd.children[c] == ji) { bn.parent = k; break; }
        }
        if (bn.parent >= 0) {
            int pb = -1;
            for (int q = 0; q < boneCount; ++q)
                if (skin.joints[q] == bn.parent) { pb = q; break; }
            bn.parent = pb;
        }
        const std::string& nm = model.nodes[ji].name;
        bn.name = nm;
        // 绑定姿态的局部 TRS，动画采样时作为没有通道覆盖的骨骼的默认值
        const tinygltf::Node& jn = model.nodes[ji];
        bn.bindT = jn.translation.size() == 3 ? Vec3((float)jn.translation[0], (float)jn.translation[1], (float)jn.translation[2]) : Vec3(0, 0, 0);
        bn.bindR = jn.rotation.size() == 4 ? Vec4((float)jn.rotation[0], (float)jn.rotation[1], (float)jn.rotation[2], (float)jn.rotation[3]) : Vec4(0, 0, 0, 1);
        bn.bindS = jn.scale.size() == 3 ? Vec3((float)jn.scale[0], (float)jn.scale[1], (float)jn.scale[2]) : Vec3(1, 1, 1);
        bn.node = ji;
        out.bones.push_back(bn);
    }
    // 保存一份“原始”骨骼位置，用于后面估算骨架尺寸与骨骼胶囊
    std::vector<Vec3> boneRawPos;
    boneRawPos.reserve((size_t)boneCount);
    for (int b = 0; b < boneCount; ++b) boneRawPos.push_back(out.bones[b].pos);

    // 统计骨架包围盒只为算出一个基准半径：取骨架高度（Y 向跨度）的 5%，
    // 这个比例只是让极细的骨骼在调试显示时不至于看不见
    Vec3 skelMin(1e30f, 1e30f, 1e30f), skelMax(-1e30f, -1e30f, -1e30f);
    for (int b = 0; b < boneCount; ++b) {
        skelMin.x = std::fmin(skelMin.x, boneRawPos[b].x);
        skelMin.y = std::fmin(skelMin.y, boneRawPos[b].y);
        skelMin.z = std::fmin(skelMin.z, boneRawPos[b].z);
        skelMax.x = std::fmax(skelMax.x, boneRawPos[b].x);
        skelMax.y = std::fmax(skelMax.y, boneRawPos[b].y);
        skelMax.z = std::fmax(skelMax.z, boneRawPos[b].z);
    }
    Vec3 skelSize = skelMax - skelMin;
    float skelHeight = std::fmax(skelSize.y, 1e-3f);
    float baseRadius = skelHeight * 0.05f;

    // 为每根有父节点的骨骼生成可视化胶囊：段的两端取“骨骼自身位置”与
    // “最近子骨骼的位置”（没有子骨骼时退化成一个点），
    // 这样胶囊能顺着骨骼链的方向摆放。
    // 特殊情况：段长超过骨架高度 35% 说明父子关系异常（如跨肢体的连接），
    // 此时把段退化为单点，只画一个小球，避免出现横穿全身的巨大胶囊
    for (int b = 0; b < boneCount; ++b) {
        int pb = out.bones[b].parent;
        bool hasParent = (pb >= 0 && pb < boneCount);
        // 根骨骼没有父骨骼，不画胶囊
        if (!hasParent) {
            out.bones[b].halfLen = 0.0f;
            out.bones[b].radius = 0.0f;
            continue;
        }

        // 找距离最近的子骨骼，用它的位置作为胶囊末端
        float childSpan = 1e30f;
        int nearestChild = -1;
        for (int c = 0; c < boneCount; ++c)
            if (out.bones[c].parent == b) {
                float dd = (boneRawPos[c] - boneRawPos[b]).length();
                if (dd < childSpan) { childSpan = dd; nearestChild = c; }
            }

        Vec3 segA = boneRawPos[b];
        Vec3 segB = boneRawPos[b];
        float refSpan = (boneRawPos[b] - boneRawPos[pb]).length();
        if (nearestChild >= 0 && childSpan < 1e30f * 0.5f && childSpan > 1e-5f) {
            segB = boneRawPos[nearestChild];
            refSpan = childSpan;
        }

        if (refSpan > skelHeight * 0.35f) {
            segB = boneRawPos[b];
            if (refSpan <= 1e-5f) refSpan = skelHeight * 0.05f;
        }

        Vec3 axis = segB - segA;
        float span = axis.length();
        // 有长度时朝向沿段方向；否则退回“指向父骨骼的方向”；再不行就用 +Y
        if (span > 1e-5f) {
            out.bones[b].dir = axis / span;
        } else if (refSpan > 1e-5f) {
            out.bones[b].dir = (boneRawPos[b] - boneRawPos[pb]) / refSpan;
        } else {
            out.bones[b].dir = Vec3(0, 1, 0);
        }

        out.bones[b].halfLen = span * 0.5f;
        // 胶囊半径取段长的 12%，并夹在 [baseRadius, 0.15] 之间，既保证可见又不至于粗到糊在一起
        out.bones[b].radius = std::fmin(std::fmax(refSpan * 0.12f, baseRadius), 0.15f);
        out.bones[b].pos = (segA + segB) * 0.5f;
    }
    out.ibm.assign(ibm.begin(), ibm.begin() + boneCount * 16);
    if (!out.mesh.create(verts.data(), vcount, indices.data(), (int)indices.size())) return false;

    // 基础色贴图：从材质里读 baseColorFactor（乘色）与 baseColorTexture（贴图），
    // 每个 primitive 单独尝试，读到第一张贴图就停止。
    // 材质参数统一存放在 tinygltf 的 mat.values 里，这里按名字取出 NumberValue 再解释
    out.hasBaseColorTex = false;
    out.baseColorW = 0;
    out.baseColorH = 0;
    out.baseColorPixels.clear();
    out.baseColorFactor[0] = out.baseColorFactor[1] = out.baseColorFactor[2] = out.baseColorFactor[3] = 1.0f;
    for (size_t pi = 0; pi < mesh->primitives.size() && !out.hasBaseColorTex; ++pi) {
        int mi = mesh->primitives[pi].material;
        if (mi < 0 || mi >= (int)model.materials.size()) continue;
        const tinygltf::Material& mat = model.materials[mi];
        auto itFactor = mat.values.find("baseColorFactor");
        if (itFactor != mat.values.end() && itFactor->second.number_array.size() >= 4) {
            tinygltf::ColorValue cf = itFactor->second.ColorFactor();
            out.baseColorFactor[0] = (float)cf[0];
            out.baseColorFactor[1] = (float)cf[1];
            out.baseColorFactor[2] = (float)cf[2];
            out.baseColorFactor[3] = (float)cf[3];
        }
        auto itTex = mat.values.find("baseColorTexture");
        if (itTex == mat.values.end()) continue;
        int ti = itTex->second.TextureIndex();
        if (ti < 0 || ti >= (int)model.textures.size()) continue;
        int si = model.textures[ti].source;
        if (si < 0 || si >= (int)model.images.size()) continue;
        const tinygltf::Image& img = model.images[si];
        int w = img.width, h = img.height, comp = img.component;
        // 只接受 3 或 4 通道的已解码图像，其它（如灰度图、解码失败的占位图）跳过
        if (w <= 0 || h <= 0 || (comp != 3 && comp != 4)) continue;
        if (img.image.size() < (size_t)w * h * comp) continue;
        out.baseColorPixels.assign((size_t)w * h * 4, 255);
        // 3 通道时补出不透明的 alpha，统一转成 RGBA8
        for (size_t k = 0; k < (size_t)w * h; ++k) {
            out.baseColorPixels[k * 4 + 0] = img.image[k * comp + 0];
            out.baseColorPixels[k * 4 + 1] = img.image[k * comp + 1];
            out.baseColorPixels[k * 4 + 2] = img.image[k * comp + 2];
            out.baseColorPixels[k * 4 + 3] = (comp == 4) ? img.image[k * comp + 3] : 255;
        }
        out.baseColorW = w;
        out.baseColorH = h;
        out.hasBaseColorTex = true;
    }

    // 蒙皮节点自身的 TRS 合成根变换矩阵。模型网格坐标是相对该节点，
    // 需要先乘这个矩阵才回到模型空间，渲染时再叠加世界位置。
    // 列主序手工展开，写法与 computeSkinnedBoneMats 保持一致
    out.anims.clear();
    out.rootTransform = Mat4::identity();
    const tinygltf::Node* mn = (meshNode >= 0 && meshNode < nodeCount) ? &model.nodes[meshNode] : nullptr;
    if (mn) {
        Vec3 t = mn->translation.size() == 3 ? Vec3((float)mn->translation[0], (float)mn->translation[1], (float)mn->translation[2]) : Vec3(0, 0, 0);
        Vec3 r = mn->rotation.size() == 4 ? Vec3((float)mn->rotation[0], (float)mn->rotation[1], (float)mn->rotation[2]) : Vec3(0, 0, 0);
        float rw = mn->rotation.size() == 4 ? (float)mn->rotation[3] : 1.0f;
        Vec3 s = mn->scale.size() == 3 ? Vec3((float)mn->scale[0], (float)mn->scale[1], (float)mn->scale[2]) : Vec3(1, 1, 1);
        float xx = r.x * r.x, yy = r.y * r.y, zz = r.z * r.z;
        float xy = r.x * r.y, xz = r.x * r.z, yz = r.y * r.z;
        float wx = rw * r.x, wy = rw * r.y, wz = rw * r.z;
        double* m = out.rootTransform.m;
        m[0] = (1 - 2 * (yy + zz)) * s.x; m[1] = (2 * (xy + wz)) * s.x; m[2] = (2 * (xz - wy)) * s.x; m[3] = 0.0f;
        m[4] = (2 * (xy - wz)) * s.y; m[5] = (1 - 2 * (xx + zz)) * s.y; m[6] = (2 * (yz + wx)) * s.y; m[7] = 0.0f;
        m[8] = (2 * (xz + wy)) * s.z; m[9] = (2 * (yz - wx)) * s.z; m[10] = (1 - 2 * (xx + yy)) * s.z; m[11] = 0.0f;
        m[12] = t.x; m[13] = t.y; m[14] = t.z; m[15] = 1.0f;
    }

    // 动画读取：先一次性把所有 sampler 的输入（时间）与输出（值）缓存下来，
    // 同一 sampler 可能被多个 channel 复用，缓存可避免重复解析。
    // 动画时长取所有输入时间序列的最后一个值中的最大值
    for (size_t ai = 0; ai < model.animations.size(); ++ai) {
        const tinygltf::Animation& an = model.animations[ai];
        SkinnedAnim sa;
        sa.name = an.name.empty() ? ("anim" + std::to_string(ai)) : an.name;
        std::vector<std::vector<float>> cacheIn(an.samplers.size());
        std::vector<std::vector<float>> cacheOut(an.samplers.size());
        std::vector<int> cacheInC(an.samplers.size(), 0), cacheOutC(an.samplers.size(), 0);
        std::vector<unsigned char> cacheOk(an.samplers.size(), 0);
        for (size_t si = 0; si < an.samplers.size(); ++si) {
            const tinygltf::AnimationSampler& s = an.samplers[si];
            if (accFloat(model, s.input, cacheIn[si], cacheInC[si]) &&
                accFloat(model, s.output, cacheOut[si], cacheOutC[si])) {
                cacheOk[si] = 1;
                if (!cacheIn[si].empty()) sa.duration = std::fmax(sa.duration, cacheIn[si].back());
            }
        }
        for (size_t ci = 0; ci < an.channels.size(); ++ci) {
            const tinygltf::AnimationChannel& ch = an.channels[ci];
            int node = ch.target_node;
            int si = ch.sampler;
            if (node < 0 || node >= nodeCount) continue;
            if (si < 0 || si >= (int)an.samplers.size() || !cacheOk[si]) continue;
            // 只保留目标节点确实是骨骼的通道，其余（相机、光源、网格节点的动画）被忽略
            int bt = -1;
            for (int q = 0; q < boneCount; ++q)
                if (out.bones[q].node == node) { bt = q; break; }
            if (bt < 0) continue;
            // path 编码：0 平移 1 旋转 2 缩放；缩放通常不需要但一并支持
            int path = -1;
            if (ch.target_path == "translation") path = 0;
            else if (ch.target_path == "rotation") path = 1;
            else if (ch.target_path == "scale") path = 2;
            else continue;
            SkinnedAnimChannel sc;
            sc.bone = bt;
            sc.path = path;
            sc.times = cacheIn[si];
            sc.values = cacheOut[si];
            sc.count = (int)(cacheIn[si].size());
            sc.comps = cacheOutC[si];
            // step 用 1e9 表示“阶跃插值”（STEP）：采样时把插值比例放大到几乎不可分辨，
            // 效果等价于取左端点值；线性插值则为 0
            sc.step = std::fmax(1e-6f, an.samplers[si].interpolation == "STEP" ? 1e9f : 0.0f);
            sa.channels.push_back(std::move(sc));
        }
        if (!sa.channels.empty()) out.anims.push_back(std::move(sa));
    }
    // pose 按骨骼数预分配，之后采样时直接就地覆盖，避免每帧重新分配
    out.pose.assign(out.bones.size(), SkinnedBonePose());
    return true;
}

// 在指定动画的 time 时刻采样出每根骨骼的局部 TRS。
// 先全部填成绑定姿态，再用动画通道覆盖被驱动的分量（未被动画驱动的分量保持绑定值）。
// 每个通道在自己的时间序列上做线性插值：
//   时间先夹到首尾范围内；用一个向前推进的下标找出相邻关键帧 lo/hi；frac 为段内比例；
//   旋转做“最短弧”插值（两个四元数点积为负时先取反其一），之后重新归一化。
// 传入超范围或无效的 animIdx 时只输出绑定姿态
void sampleSkinnedAnim(const SkinnedMesh& sm, int animIdx, float time, std::vector<SkinnedBonePose>& pose) {
    if (pose.size() != sm.bones.size()) pose.assign(sm.bones.size(), SkinnedBonePose());
    for (size_t b = 0; b < sm.bones.size(); ++b) {
        pose[b].t = sm.bones[b].bindT;
        pose[b].r = sm.bones[b].bindR;
        pose[b].s = sm.bones[b].bindS;
    }
    if (animIdx < 0 || animIdx >= (int)sm.anims.size()) return;
    const SkinnedAnim& an = sm.anims[animIdx];
    for (size_t c = 0; c < an.channels.size(); ++c) {
        const SkinnedAnimChannel& ch = an.channels[c];
        if (ch.bone < 0 || ch.bone >= (int)pose.size()) continue;
        int comps = ch.comps;
        if (comps <= 0 || ch.count <= 0 || ch.times.empty()) continue;
        if ((int)ch.values.size() < ch.count * comps) continue;
        float t = time;
        if (t < ch.times.front()) t = ch.times.front();
        if (t > ch.times.back()) t = ch.times.back();
        // 线性前进找到第一个时间不小于 t 的关键帧；通道的关键帧数通常很少，
        // 这里不用二分查找，简单循环即可
        int hi = 1;
        while (hi < ch.count && ch.times[hi] < t) ++hi;
        int lo = hi - 1;
        if (hi >= ch.count) { hi = ch.count - 1; lo = hi; }
        if (lo < 0) lo = 0;
        float frac = 0.0f;
        if (hi > lo) {
            float span = ch.times[hi] - ch.times[lo];
            frac = span > 1e-6f ? (t - ch.times[lo]) / span : 0.0f;
        }
        const float* a = &ch.values[(size_t)lo * comps];
        const float* b2 = &ch.values[(size_t)hi * comps];
        if (ch.path == 0 && comps >= 3) {
            pose[ch.bone].t = Vec3(a[0] + (b2[0] - a[0]) * frac,
                                   a[1] + (b2[1] - a[1]) * frac,
                                   a[2] + (b2[2] - a[2]) * frac);
        } else if (ch.path == 1 && comps >= 4) {
            // 四元数最短弧：点积为负说明两个四元数表示相反的旋转方向，先翻转其一
            Vec4 q0(a[0], a[1], a[2], a[3]);
            Vec4 q1(b2[0], b2[1], b2[2], b2[3]);
            if (q0.dot(q1) < 0.0f) q1 = q1 * -1.0f;
            Vec4 q(q0.x + (q1.x - q0.x) * frac,
                   q0.y + (q1.y - q0.y) * frac,
                   q0.z + (q1.z - q0.z) * frac,
                   q0.w + (q1.w - q0.w) * frac);
            // 线性插值得到的四元数不是单位长度，必须归一化否则骨骼会被缩放
            float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            pose[ch.bone].r = len > 1e-6f ? Vec4(q.x / len, q.y / len, q.z / len, q.w / len) : Vec4(0, 0, 0, 1);
        } else if (ch.path == 2 && comps >= 3) {
            pose[ch.bone].s = Vec3(a[0] + (b2[0] - a[0]) * frac,
                                   a[1] + (b2[1] - a[1]) * frac,
                                   a[2] + (b2[2] - a[2]) * frac);
        }
    }
}

// 计算“骨骼矩阵”数组，即上传给着色器的每骨骼 skinning 矩阵 = 骨骼世界变换 * 逆绑定矩阵。
// 局部矩阵由 pose 的 TRS 合成（列主序，手动展开四元数转矩阵）。
// 由于 bone.parent 存的是骨骼下标，而 world 数组按“节点编号”索引，
// 这里用 done 标记已算好的节点，并且为每根骨骼向上找一条到根（或到已算出的祖先）的链，
// 再从链顶往下依次累乘，避免递归与重复计算。
// 返回时 outMats 里每根骨骼 16 个 float，无效骨骼的矩阵保持全 0
void computeSkinnedBoneMats(const SkinnedMesh& sm, const std::vector<SkinnedBonePose>& pose,
                            const Mat4& rootWorld, std::vector<float>& outMats) {
    size_t n = sm.bones.size();
    outMats.assign(n * 16, 0.0f);
    if (n == 0 || pose.size() != n) return;
    if (sm.ibm.size() < n * 16) return;
    int nodeCount = 0;
    for (size_t i = 0; i < n; ++i) nodeCount = std::max(nodeCount, sm.bones[i].node + 1);
    if (nodeCount <= 0) return;
    std::vector<Mat4> world((size_t)nodeCount, Mat4::identity());
    std::vector<unsigned char> done((size_t)nodeCount, 0);
    for (size_t b = 0; b < n; ++b) {
        // chain 里存的是骨骼下标，从当前骨骼一路向上；遇到失效父节点
        // 或已经算过的祖先就停止，保证不会死循环
        int chain[64];
        int chainLen = 0;
        int cur = (int)b;
        while (cur >= 0 && chainLen < 64) {
            chain[chainLen++] = cur;
            int p = sm.bones[cur].parent;
            if (p < 0 || p >= (int)n) break;
            int pn = sm.bones[p].node;
            if (pn >= 0 && pn < nodeCount && done[(size_t)pn]) break;
            cur = p;
        }
        // 从链顶（最靠祖先的一根）向下依次计算世界矩阵，保证父先于子
        for (int k = chainLen - 1; k >= 0; --k) {
            int bi = chain[k];
            const SkinnedBone& bone = sm.bones[bi];
            const SkinnedBonePose& bp = pose[bi];
            float xx = bp.r.x * bp.r.x, yy = bp.r.y * bp.r.y, zz = bp.r.z * bp.r.z;
            float xy = bp.r.x * bp.r.y, xz = bp.r.x * bp.r.z, yz = bp.r.y * bp.r.z;
            float wx = bp.r.w * bp.r.x, wy = bp.r.w * bp.r.y, wz = bp.r.w * bp.r.z;
            // 局部矩阵 = 旋转(四元数) * 缩放，再将平移写入第 4 列
            Mat4 local = Mat4::identity();
            double* m = local.m;
            m[0] = (1 - 2 * (yy + zz)) * bp.s.x; m[1] = (2 * (xy + wz)) * bp.s.x; m[2] = (2 * (xz - wy)) * bp.s.x;
            m[4] = (2 * (xy - wz)) * bp.s.y; m[5] = (1 - 2 * (xx + zz)) * bp.s.y; m[6] = (2 * (yz + wx)) * bp.s.y;
            m[8] = (2 * (xz + wy)) * bp.s.z; m[9] = (2 * (yz - wx)) * bp.s.z; m[10] = (1 - 2 * (xx + yy)) * bp.s.z;
            m[12] = bp.t.x; m[13] = bp.t.y; m[14] = bp.t.z; m[15] = 1.0f;
            // 父节点链断了或父节点还没算出来时，直接用 rootWorld 当父矩阵
            int pi = bone.parent;
            Mat4 parentW = rootWorld;
            if (pi >= 0 && pi < (int)n) {
                int pn = sm.bones[pi].node;
                if (pn >= 0 && pn < nodeCount) parentW = world[(size_t)pn];
            }
            // 父 * 子，按列主序手工相乘：结果的第 c 列 4 个分量逐个点乘
            Mat4 res = Mat4::identity();
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    float sum = 0.0f;
                    for (int kk = 0; kk < 4; ++kk) sum += parentW.m[kk * 4 + r] * local.m[c * 4 + kk];
                    res.m[c * 4 + r] = sum;
                }
            if (bone.node >= 0 && bone.node < nodeCount) {
                world[(size_t)bone.node] = res;
                done[(size_t)bone.node] = 1;
            }
        }
    }
    // 最终 skinning 矩阵 = 骨骼世界矩阵 * 逆绑定矩阵（同样按列主序手算）
    for (size_t b = 0; b < n; ++b) {
        int bn = sm.bones[b].node;
        if (bn < 0 || bn >= nodeCount) continue;
        const Mat4& jw = world[(size_t)bn];
        const float* im = &sm.ibm[b * 16];
        float* dst = &outMats[b * 16];
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (int kk = 0; kk < 4; ++kk) sum += jw.m[kk * 4 + r] * im[c * 4 + kk];
                dst[c * 4 + r] = sum;
            }
    }
}
