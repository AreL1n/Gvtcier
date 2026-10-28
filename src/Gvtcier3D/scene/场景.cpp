#include "Gvtcier.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <algorithm>
#include <chrono>
#include <functional>
#include "stb/stb_image.h"

// 场景内全部网格资源的持有者。放在独立结构里并以指针挂在 Scene 上，是为了让头文件不必暴露 Mesh 的完整定义
struct SceneMeshes {
    // 通过 Gvtcier_Scene_LoadModel / LoadModelFile 加载的“单一共享”网格，
    // 每次加载都会整体覆盖这三个网格，因此后一次加载会顶掉前一次的几何数据
    Mesh objMesh_, gltfMesh_;
    Mesh fileMesh_;
    // 纹理缓存：键为资源名或文件路径，值为纹理 ID。同一路径只解码一次
    std::map<std::string, GvtId<Texture>> pngTexs_;
    // 模型缓存：键为文件路径，值是该路径独占注册出来的网格 ID
    std::map<std::string, GvtId<Mesh>> modelMeshs_;
};

// 骨骼蒙皮网格的唯一持有者，场景同时只支持一个蒙皮模型
struct SceneSkinned {
    SkinnedMesh skinnedMesh_;
};

Scene::Scene() : meshes_(new SceneMeshes()), skinned_(new SceneSkinned()) {}

static void assignUid(Object& o);

// 析构不释放 GPU 资源，显式资源释放必须调用 Gvtcier_Scene_Destroy
Scene::~Scene() {
}

// 以下一组访问器返回内置网格与纹理的 ID。ID 由 Gvtcier_Scene_Build 一次性注册，
// 使用方必须再经 Gvtcier_Scene_GetMesh / GetTex 取值并判空，不能跨帧缓存返回的裸指针
GvtId<Mesh> Scene::cubeMesh() { return cubeMeshId_; }
GvtId<Mesh> Scene::planeMesh() { return planeMeshId_; }
GvtId<Mesh> Scene::sphereMesh() { return sphereMeshId_; }
GvtId<Mesh> Scene::coneMesh() { return coneMeshId_; }
GvtId<Mesh> Scene::planeCircleMesh() { return planeCircleMeshId_; }
GvtId<Mesh> Scene::planeTriMesh() { return planeTriMeshId_; }
GvtId<Mesh> Scene::cylinderMesh() { return cylinderMeshId_; }
GvtId<Mesh> Scene::torusMesh() { return torusMeshId_; }
GvtId<Mesh> Scene::colliderBoxMesh() { return colliderBoxMeshId_; }
GvtId<Mesh> Scene::colliderCylinderMesh() { return colliderCylinderMeshId_; }
// 1x1 纯白纹理，作为“没有贴图”时的占位，避免着色器分支里出现空纹理
GvtId<Texture> Scene::whiteTex() { return whiteTexId_; }
// 只有索引数大于 0 才算成功载入了蒙皮模型
bool Scene::hasSkinned() const { return skinned_->skinnedMesh_.mesh.indexCount() > 0; }
SkinnedMesh& Scene::skinnedMesh() { return skinned_->skinnedMesh_; }
const SkinnedMesh& Scene::skinnedMesh() const { return skinned_->skinnedMesh_; }
GvtId<Texture> Scene::skinnedTex() { return skinnedTexId_; }
Mesh* Scene::Gvtcier_Scene_GetSkinnedMesh() { return &skinned_->skinnedMesh_.mesh; }
const Mesh* Scene::Gvtcier_Scene_GetSkinnedMesh() const { return &skinned_->skinnedMesh_.mesh; }
Texture* Scene::Gvtcier_Scene_GetSkinnedTex() { return texStore_.Gvtcier_Object_Get(skinnedTexId_); }
const Texture* Scene::Gvtcier_Scene_GetSkinnedTex() const { return texStore_.Gvtcier_Object_Get(skinnedTexId_); }

// 资源存储的取用入口。ID 无效（未注册或已被销毁导致世代号过期）时返回 nullptr
Mesh* Scene::Gvtcier_Scene_GetMesh(GvtId<Mesh> id) { return meshStore_.Gvtcier_Object_Get(id); }
const Mesh* Scene::Gvtcier_Scene_GetMesh(GvtId<Mesh> id) const { return meshStore_.Gvtcier_Object_Get(id); }
Texture* Scene::Gvtcier_Scene_GetTex(GvtId<Texture> id) { return texStore_.Gvtcier_Object_Get(id); }
const Texture* Scene::Gvtcier_Scene_GetTex(GvtId<Texture> id) const { return texStore_.Gvtcier_Object_Get(id); }
// 注册一份纹理并返回其 ID，所有权随即转交 texStore_
GvtId<Texture> Scene::Gvtcier_Scene_RegisterTexture(Texture&& tex) { return texStore_.Gvtcier_Object_Register(std::move(tex)); }
// 先释放纹理的 GPU 句柄再销毁登记项；顺序不能反，否则拿不到 Texture 去调 destroy
void Scene::Gvtcier_Scene_UnregisterTexture(GvtId<Texture> id) {
    Texture* t = texStore_.Gvtcier_Object_Get(id);
    if (t) t->destroy();
    texStore_.Gvtcier_Object_Destroy(id);
}

// 把内存中的 PNG 字节解码为 RGBA8 纹理。返回 false 表示数据为空、解码失败或尺寸异常
static bool decodePng(const std::vector<unsigned char>& bytes, Texture& out) {
    if (bytes.empty()) return false;
    int w = 0, h = 0, comp = 0;
    // 翻转 Y 轴：图像文件的行序是自上而下，而引擎的 UV 原点在下方，翻转后可直接按常规 UV 采样
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* img = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &comp, 4);
    // 8192 是贴图尺寸上限，超过说明资源异常，直接拒绝以免显存爆掉
    if (!img || w <= 0 || h <= 0 || w > 8192 || h > 8192) {
        if (img) stbi_image_free(img);
        return false;
    }
    std::vector<unsigned char> px(img, img + (size_t)w * h * 4);
    stbi_image_free(img);
    return out.create(w, h, px.data());
}

// 按文件路径加载 PNG 纹理，失败时回退到白色占位纹理。
// 同一路径第二次调用直接命中缓存，不会重复注册纹理
GvtId<Texture> Scene::Gvtcier_Scene_LoadPngTexture(const char* path) {
    if (!path || !path[0]) return whiteTexId_;
    auto it = meshes_->pngTexs_.find(path);
    if (it != meshes_->pngTexs_.end()) return it->second;
    std::vector<unsigned char> bytes;
    if (!Gvtcier_Res_ReadFile(path, bytes) || bytes.empty()) return whiteTexId_;
    Texture t;
    if (!decodePng(bytes, t)) return whiteTexId_;
    GvtId<Texture> id = texStore_.Gvtcier_Object_Register(std::move(t));
    meshes_->pngTexs_[path] = id;
    return id;
}

// 按资源名读取字节，三级回退：先查打包进可执行文件的资源表，再查普通文件，
// 最后按内置贴图表 kGvVeinTexs 把资源名映射到 assets/Vein 下的实际文件名
static bool readResBytes(const char* resName, std::vector<unsigned char>& out) {
    int len = 0;
    const void* data = res_get(resName, &len);
    if (data && len > 0) {
        out.assign((const unsigned char*)data, (const unsigned char*)data + len);
        return true;
    }
    if (Gvtcier_Res_ReadFile(resName, out) && !out.empty()) return true;
    for (int i = 0; i < kGvVeinTexCount; ++i) {
        if (strcmp(kGvVeinTexs[i].res, resName) == 0) {
            std::string p = std::string("assets/Vein/") + kGvVeinTexs[i].file;
            return Gvtcier_Res_ReadFile(p.c_str(), out) && !out.empty();
        }
    }
    return false;
}

// 按资源名加载 PNG 纹理（gmap 里写的贴图名走这里），同样带缓存与白色占位回退
GvtId<Texture> Scene::Gvtcier_Scene_LoadPngRes(const char* resName) {
    if (!resName || !resName[0]) return whiteTexId_;
    auto it = meshes_->pngTexs_.find(resName);
    if (it != meshes_->pngTexs_.end()) return it->second;
    std::vector<unsigned char> bytes;
    if (!readResBytes(resName, bytes)) return whiteTexId_;
    Texture t;
    if (!decodePng(bytes, t)) return whiteTexId_;
    GvtId<Texture> id = texStore_.Gvtcier_Object_Register(std::move(t));
    meshes_->pngTexs_[resName] = id;
    return id;
}

// 开场预载用：和 LoadPngRes 走同一条路，只是失败时不再悄悄给白图，而是明说没解开
bool Scene::Gvtcier_Scene_PreloadTex(const char* resName) {
    if (!resName || !resName[0]) return false;
    auto it = meshes_->pngTexs_.find(resName);
    if (it != meshes_->pngTexs_.end()) return it->second != whiteTexId_;
    std::vector<unsigned char> bytes;
    if (!readResBytes(resName, bytes)) return false;
    Texture t;
    if (!decodePng(bytes, t)) return false;
    GvtId<Texture> id = texStore_.Gvtcier_Object_Register(std::move(t));
    meshes_->pngTexs_[resName] = id;
    return true;
}

// 内置贴图表（资源名 -> 文件名）的访问器，表本身由资源表同步脚本生成
static const GvVeinTexEntry* builtinTexs() {
    return kGvVeinTexs;
}

static int builtinTexCount() {
    return kGvVeinTexCount;
}

// 双线性插值缩放 RGBA8 图像。用像素中心对齐（+0.5 -0.5）采样并夹取边界索引，
// 目的是让不同尺寸的贴图能组成纹理数组（纹理数组要求各层等尺寸）
static void resizeRgba(const std::vector<unsigned char>& src, int sw, int sh,
                       std::vector<unsigned char>& dst, int dw, int dh) {
    dst.resize((size_t)dw * dh * 4);
    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * sh / (float)dh - 0.5f;
        int y0 = (int)std::floor(fy); float ty = fy - y0;
        int y1 = y0 + 1;
        y0 = y0 < 0 ? 0 : (y0 > sh - 1 ? sh - 1 : y0);
        y1 = y1 < 0 ? 0 : (y1 > sh - 1 ? sh - 1 : y1);
        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * sw / (float)dw - 0.5f;
            int x0 = (int)std::floor(fx); float tx = fx - x0;
            int x1 = x0 + 1;
            x0 = x0 < 0 ? 0 : (x0 > sw - 1 ? sw - 1 : x0);
            x1 = x1 < 0 ? 0 : (x1 > sw - 1 ? sw - 1 : x1);
            for (int c = 0; c < 4; ++c) {
                float a = src[((size_t)y0 * sw + x0) * 4 + c];
                float b = src[((size_t)y0 * sw + x1) * 4 + c];
                float p = src[((size_t)y1 * sw + x0) * 4 + c];
                float q = src[((size_t)y1 * sw + x1) * 4 + c];
                float v = a * (1 - tx) * (1 - ty) + b * tx * (1 - ty) + p * (1 - tx) * ty + q * tx * ty;
                dst[((size_t)y * dw + x) * 4 + c] = (unsigned char)(v + 0.5f);
            }
        }
    }
}

// 把多张 PNG 合成一张纹理数组（例如地形分层贴图）。以第一张尺寸为基准，
// 其余尺寸不一致的都会经过拉伸，因此数组各层对齐；缓存键是把所有资源名用 '|' 拼接
GvtId<Texture> Scene::Gvtcier_Scene_LoadPngArrayRes(const char* const* resNames, int n) {
    if (!resNames || n <= 0) return whiteTexId_;
    std::string key;
    for (int i = 0; i < n; ++i) { key += resNames[i]; key += '|'; }
    auto it = meshes_->pngTexs_.find(key);
    if (it != meshes_->pngTexs_.end()) return it->second;
    std::vector<std::vector<unsigned char>> px(n);
    int w = 0, h = 0;
    for (int i = 0; i < n; ++i) {
        if (!readResBytes(resNames[i], px[i])) return whiteTexId_;
        int len = (int)px[i].size();
        int sw = 0, sh = 0, comp = 0;
        stbi_set_flip_vertically_on_load_thread(1);
        unsigned char* img = stbi_load_from_memory(px[i].data(), len, &sw, &sh, &comp, 4);
        if (!img || sw <= 0 || sh <= 0) { if (img) stbi_image_free(img); return whiteTexId_; }
        std::vector<unsigned char> raw(img, img + (size_t)sw * sh * 4);
        stbi_image_free(img);
        // 第一张决定数组的基准尺寸，后续层要么尺寸完全相同直接复用，要么缩放对齐
        if (w == 0) { w = sw; h = sh; px[i] = std::move(raw); }
        else if (sw == w && sh == h) px[i] = std::move(raw);
        else resizeRgba(raw, sw, sh, px[i], w, h);
    }
    std::vector<const unsigned char*> ptrs(n);
    for (int i = 0; i < n; ++i) ptrs[i] = px[i].data();
    Texture t;
    if (!t.createArray(w, h, n, ptrs.data())) return whiteTexId_;
    GvtId<Texture> id = texStore_.Gvtcier_Object_Register(std::move(t));
    meshes_->pngTexs_[key] = id;
    return id;
}

// 把素材库中的显示名翻译成打包资源名；不是内置贴图则返回 nullptr
const char* Scene::Gvtcier_Scene_BuiltinResName(const char* name) {
    if (!name || !name[0]) return nullptr;
    for (int i = 0; i < builtinTexCount(); ++i)
        if (strcmp(name, builtinTexs()[i].name) == 0) return builtinTexs()[i].res;
    return nullptr;
}

// 按内置贴图显示名加载纹理，找不到或加载失败都回退到白色纹理
GvtId<Texture> Scene::Gvtcier_Scene_BuiltinTexture(const char* name) {
    if (!name || !name[0]) return whiteTexId_;
    for (int i = 0; i < builtinTexCount(); ++i) {
        if (strcmp(name, builtinTexs()[i].name) == 0)
            return Gvtcier_Scene_LoadPngRes(builtinTexs()[i].res);
    }
    return whiteTexId_;
}

bool Scene::Gvtcier_Scene_HasBuiltinTexture(const char* name) const {
    if (!name || !name[0]) return false;
    for (int i = 0; i < builtinTexCount(); ++i)
        if (strcmp(name, builtinTexs()[i].name) == 0) return true;
    return false;
}

// 越界索引返回空串而不是 nullptr，方便 UI 直接显示
const char* Scene::Gvtcier_Scene_BuiltinTextureName(int i) const {
    if (i < 0 || i >= builtinTexCount()) return "";
    return builtinTexs()[i].name;
}

int Scene::Gvtcier_Scene_BuiltinTextureCount() const {
    return builtinTexCount();
}

// 创建场景的常驻资源：内置网格、白色纹理，并清空对象表。
// 会先清空旧的网格与纹理存储，因此调用前必须确保没有外部持有旧 ID 的指针
bool Scene::Gvtcier_Scene_Build() {
    meshStore_.Gvtcier_Object_Clear();
    texStore_.Gvtcier_Object_Clear();
    meshes_->pngTexs_.clear();
    meshes_->modelMeshs_.clear();

    // 这些网格的尺寸都按“单位大小 1”构造，实际大小由对象的 scale 决定
    cubeMeshId_ = meshStore_.Gvtcier_Object_Register(makeCube(1.0f));
    planeMeshId_ = meshStore_.Gvtcier_Object_Register(makePlane(1.0f, 1.0f, 1, 1.0f));
    imageMeshId_ = meshStore_.Gvtcier_Object_Register(makePlaneFacing(1.0f, 1.0f));
    sphereMeshId_ = meshStore_.Gvtcier_Object_Register(makeSphere(0.5f, 32, 32));
    coneMeshId_ = meshStore_.Gvtcier_Object_Register(makeCone(0.5f, 24));
    planeCircleMeshId_ = meshStore_.Gvtcier_Object_Register(makePlaneCircle(0.5f, 32));
    planeTriMeshId_ = meshStore_.Gvtcier_Object_Register(makePlaneTriangle(0.5f));
    cylinderMeshId_ = meshStore_.Gvtcier_Object_Register(makeCylinder(0.5f, 1.0f, 24));
    // 圆环：主半径 0.35、管半径 0.15，绕行 24 段、截面 16 段
    torusMeshId_ = meshStore_.Gvtcier_Object_Register(makeTorus(0.35f, 0.15f, 24, 16));
    // 碰撞体可视化用的线框网格，与真实碰撞形状一一对应
    colliderBoxMeshId_ = meshStore_.Gvtcier_Object_Register(makeCube(1.0f));
    colliderCylinderMeshId_ = meshStore_.Gvtcier_Object_Register(makeCylinder(0.5f, 1.0f, 32));
    // 三个“空网格”槽位，供 OBJ/GLTF 加载时复用作容器
    objMeshId_ = meshStore_.Gvtcier_Object_Register(Mesh());
    gltfMeshId_ = meshStore_.Gvtcier_Object_Register(Mesh());
    fileMeshId_ = meshStore_.Gvtcier_Object_Register(Mesh());

    // 4x4 纯白纹理，尺寸取小以省显存，配合重复采样足够当作白色
    whiteTexId_ = texStore_.Gvtcier_Object_Register(makeSolidTexture(4, 4, Vec3(1.0f, 1.0f, 1.0f)));

    objects_.Gvtcier_Object_Clear();
    Gvtcier_Scene_InvalidateNameIndex();
    return true;
}

// 估算对象在世界空间的半尺寸。基础半尺寸固定为 (0.5,0.5,0.5)，即单位盒，
// 乘上对象缩放后，若存在旋转则取旋转后 AABB 的半长（|R| * half 的逐轴求和），
// 这样旋转后的包围盒始终能包住原始盒体
Vec3 Object::Gvtcier_Object_MeshHalfExtents() const {
    Vec3 local(0.5f, 0.5f, 0.5f);
    Vec3 half(std::fabs(local.x * scale.x), std::fabs(local.y * scale.y), std::fabs(local.z * scale.z));
    if (rotDeg.x == 0.0f && rotDeg.y == 0.0f && rotDeg.z == 0.0f) return half;
    Mat4 r = Quat::fromEulerDeg(rotDeg.x, rotDeg.y, rotDeg.z).toMat4();
    Vec3 ax(half.x * r.m[0], half.x * r.m[1], half.x * r.m[2]);
    Vec3 ay(half.y * r.m[4], half.y * r.m[5], half.y * r.m[6]);
    Vec3 az(half.z * r.m[8], half.z * r.m[9], half.z * r.m[10]);
    return Vec3(std::fabs(ax.x) + std::fabs(ay.x) + std::fabs(az.x),
                std::fabs(ax.y) + std::fabs(ay.y) + std::fabs(az.y),
                std::fabs(ax.z) + std::fabs(ay.z) + std::fabs(az.z));
}

// 返回对象的局部模型矩阵，带脏标记缓存：只有位置/旋转/缩放/轴心变化并把 matrixDirty 置脏后才重算。
// 变换顺序为 平移 * rotX * rotY * rotZ * 缩放 * (-pivot)，即轴心先被拉到原点再缩放旋转，
// 所以 pivot 是对象自己坐标系下的旋转中心
const Mat4& Object::Gvtcier_Object_Matrix() const {
    if (matrixDirty) {
        cachedMatrix = Mat4::translate(pos)
                     * Mat4::rotateX(deg2rad(rotDeg.x))
                     * Mat4::rotateY(deg2rad(rotDeg.y))
                     * Mat4::rotateZ(deg2rad(rotDeg.z))
                     * Mat4::scale(scale)
                     * Mat4::translate(Vec3(-pivot.x, -pivot.y, -pivot.z));
        matrixDirty = false;
    }
    return cachedMatrix;
}

// 返回世界空间的轴对齐包围盒，同样带缓存（由 boundsDirty 控制失效）。
// 注意这里的中心用的是 pos + pivot*scale，而 Gvtcier_Object_Matrix 里 pivot 是反向平移，
// 两者对 pivot 的符号处理不一致，pivot 非零时包围盒与渲染位置会偏移
const AABB3& Object::Gvtcier_Object_Bounds(const Vec3& half) const {
    if (boundsDirty) {
        Vec3 h(half.x * scale.x, half.y * scale.y, half.z * scale.z);
        Vec3 c(pos.x + pivot.x * scale.x, pos.y + pivot.y * scale.y, pos.z + pivot.z * scale.z);
        cachedBounds = makeObbAABB(c, Vec3(h.x < 0.0f ? -h.x : h.x, h.y < 0.0f ? -h.y : h.y,
                                           h.z < 0.0f ? -h.z : h.z), rotDeg);
        boundsDirty = false;
    }
    return cachedBounds;
}

// 生成对象唯一标识 uid。前缀取名字里的英文字母与数字（最长 4 位，全大写），
// 再拼接 6~10 位随机数字与 1 位校验字母。随机种子混合了时钟、栈地址与全局计数器，
// 用两次 xor-shift 与乘法做散列，目的是同一帧内连续生成也不会撞号
std::string Scene::Gvtcier_Scene_GenerateUid(const char* prefix) {
    static const char* kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static uint64_t counter = 0;
    // 计数器只保留低 63 位，避免高位溢出干扰散列
    counter = (counter + 1) & 0x7FFFFFFFFFFFFFFFull;

    const char* src = (prefix && prefix[0]) ? prefix : "GVT";
    size_t srcLen = std::strlen(src);
    std::string uid;
    uid.reserve(srcLen + 10);

    // 只保留 ASCII 字母数字，中文名等会被过滤掉，因此纯中文名最终前缀为 "GVT"
    for (size_t i = 0; i < srcLen; ++i) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z') {
            uid.push_back(c);
        }
    }
    if (uid.empty()) uid = "GVT";

    uint64_t seed = (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    seed ^= (uint64_t)(uintptr_t)&uid;
    // 0x9E3779B97F4A7C15 是 64 位黄金比例常数，用于把计数器均匀打散到整个位域
    seed ^= counter * 0x9E3779B97F4A7C15ull;
    seed ^= (uint64_t)std::hash<std::string>()(uid) * 0xBF58476D1CE4E5B9ull;
    seed ^= seed >> 33;
    seed *= 0xFF51AFD7ED558CCDull;
    seed ^= seed >> 33;

    // 6 + 0..4 位随机数字：数字位长不固定，让 uid 看起来更像手工编号
    int digits = (int)(6 + (seed % 5));
    uid.push_back('_');
    for (int i = 0; i < digits; ++i) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        uid.push_back((char)('0' + (int)((seed >> 33) % 10)));
    }
    uid.push_back(kLetters[(seed >> 21) % 26]);
    return uid;
}

// 只在该对象还没有 uid 时生成，保留从 gmap 读入的原始 uid；
// 名字也为空时用 uid 兜底，保证对象总能在面板上被识别
static void assignUid(Object& o) {
    if (o.uid.empty()) {
        std::string prefix;
        const std::string& n = o.name;
        for (size_t i = 0; i < n.size() && prefix.size() < 4; ++i) {
            unsigned char c = (unsigned char)n[i];
            if (c < 0x80) {
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    char u = (char)c;
                    if (u >= 'a' && u <= 'z') u = (char)(u - 'a' + 'A');
                    prefix.push_back(u);
                }
            }
        }
        // 名字里没有任何 ASCII 字母数字时（如纯中文），退化为 OBJ 前缀
        if (prefix.empty()) prefix = "OBJ";
        o.uid = Scene::Gvtcier_Scene_GenerateUid(prefix.c_str());
    }
    if (o.name.empty()) o.name = o.uid;
}

// 以拷贝方式添加对象，调用方对象不受影响；名称与 uid 索引随之失效，下次查找时重建
void Scene::Gvtcier_Scene_AddObject(const Object& o) {
    Object copy(o);
    assignUid(copy);
    objects_.Gvtcier_Object_Register(std::move(copy));
    nameIndexValid_ = false;
    uidIndexValid_ = false;
}

// 以移动方式添加对象并返回其 GvtId。此后对象只存在于 objects_ 存储中，
// 必须通过 ID 取用，销毁走 Gvtcier_Scene_FlushRemoves 而不是 delete
GvtId<Object> Scene::Gvtcier_Scene_RegisterObject(Object&& o) {
    assignUid(o);
    GvtId<Object> id = objects_.Gvtcier_Object_Register(std::move(o));
    nameIndexValid_ = false;
    uidIndexValid_ = false;
    return id;
}

// 返回 uid 字符串的裸指针，指向对象内部存储；对象被销毁后该指针立即失效
const char* Scene::Gvtcier_Scene_ObjectUid(GvtId<Object> id) {
    const Object* o = objects_.Gvtcier_Object_Get(id);
    return o ? o->uid.c_str() : "";
}

// 按 uid 查对象。首次查询构建哈希索引；若索引里没命中（可能出现重名导致 emplace 未插入），
// 再线性扫描一次兜底，保证能返回某个匹配的存活对象
GvtId<Object> Scene::Gvtcier_Scene_FindByUid(const char* uid) {
    if (!uid || !uid[0]) return GvtId<Object>{};
    if (!uidIndexValid_) {
        uidIndex_.clear();
        objects_.ForEachAlive([&](const Object& o, GvtId<Object> id) {
            if (!o.uid.empty()) uidIndex_.emplace(o.uid, id);
        });
        uidIndexValid_ = true;
    }
    std::unordered_map<std::string, GvtId<Object>>::const_iterator it = uidIndex_.find(uid);
    if (it != uidIndex_.end()) return it->second;
    GvtId<Object> fallback;
    objects_.ForEachAlive([&](const Object& o, GvtId<Object> id) {
        if (fallback.valid() || o.uid.empty()) return;
        if (o.uid == uid) fallback = id;
    });
    return fallback;
}

// 取对象指针。ID 已失效（对象被销毁，世代号不匹配）时返回 nullptr，使用前务必判空
Object* Scene::Gvtcier_Scene_GetObject(GvtId<Object> id) {
    return objects_.Gvtcier_Object_Get(id);
}

const Object* Scene::Gvtcier_Scene_GetObject(GvtId<Object> id) const {
    return objects_.Gvtcier_Object_Get(id);
}

// 由对象指针反查 ID：线性扫描存储区比较地址。仅适合低频调用，
// 因为对象存储在增删后可能重新分布，不能靠指针运算直接算下标
GvtId<Object> Scene::Gvtcier_Scene_IdOf(const Object* o) const {
    if (!o) return GvtId<Object>{};
    GvtId<Object> found;
    objects_.ForEachAlive([&](const Object& so, GvtId<Object> id) {
        if (found.valid()) return;
        if (&so == o) found = id;
    });
    return found;
}

size_t Scene::Gvtcier_Scene_ObjectCount() const {
    return objects_.Gvtcier_Object_Count();
}

// 取一份当前所有存活对象的 ID 快照。返回的是 ID 而非指针，
// 遍历过程中即便有对象被销毁，旧 ID 也会因为世代号失效而查不到
std::vector<GvtId<Object>> Scene::Gvtcier_Scene_ObjectIds() const {
    std::vector<GvtId<Object>> out;
    out.reserve(objects_.Gvtcier_Object_Count());
    objects_.ForEachAlive([&](const Object&, GvtId<Object> id) { out.push_back(id); });
    return out;
}

// 恢复默认天空色（浅天蓝）。场景里没有任何天空盒资源，只靠这个颜色着色
void Scene::Gvtcier_Scene_InitSkybox() {
    skyColor = Vec3(0.62f, 0.78f, 0.96f);
}

// 恢复默认光照：开启太阳光与环境光。环境光强度取太阳光的一半（1.0 * 0.5），
// 负责撑住阴影区与背光面，否则直射光一被切断里面就是死黑；太阳方向为右上后方入射
void Scene::Gvtcier_Scene_InitSun() {
    sunEnabled = true;
    ambientEnabled = true;
    ambientIntensity = 0.5f;
    ambientColor = Vec3(0.82f, 0.86f, 0.95f);
    light.dir = Vec3(0.30f, -0.74f, 0.60f).normalized();
    light.color = Vec3(1.0f, 0.97f, 0.9f);
}

// 体积光默认开启，强度 0.4，作为太阳光的可见光柱
void Scene::Gvtcier_Scene_InitVolumeLight() {
    volumeLightEnabled = true;
    volStrength = 0.4f;
}

// 雾密度默认 0.004，量级很小，保证远景只是淡雾而不是糊成一片
void Scene::Gvtcier_Scene_InitFog() {
    fogDensity = 0.004f;
}

// 把当前的外貌参数（天空、太阳、体积光、雾、后处理、环境光）整体快照到 look_，
// 用于编辑器里临时试参数后回滚
void Scene::Gvtcier_Scene_CaptureLook() {
    look_.skyColor = skyColor;
    look_.showSkybox = showSkybox;
    look_.sunEnabled = sunEnabled;
    look_.light = light;
    look_.volumeLightEnabled = volumeLightEnabled;
    look_.volStrength = volStrength;
    look_.volumetricFogEnabled = volumetricFogEnabled;
    look_.fogDensity = fogDensity;
    look_.bloomStrength = bloomStrength;
    look_.hdr = hdr;
    look_.gtaoEnabled = gtaoEnabled;
    look_.ambientEnabled = ambientEnabled;
    look_.ambientIntensity = ambientIntensity;
    look_.ambientColor = ambientColor;
    look_.valid = true;
}

// 从快照恢复外貌参数。从未快照过（look_.valid 为假）时退回各项默认值
void Scene::Gvtcier_Scene_RestoreLook() {
    if (!look_.valid) {
        Gvtcier_Scene_InitSkybox();
        Gvtcier_Scene_InitSun();
        Gvtcier_Scene_InitVolumeLight();
        Gvtcier_Scene_InitFog();
        return;
    }
    skyColor = look_.skyColor;
    showSkybox = look_.showSkybox;
    sunEnabled = look_.sunEnabled;
    light = look_.light;
    volumeLightEnabled = look_.volumeLightEnabled;
    volStrength = look_.volStrength;
    volumetricFogEnabled = look_.volumetricFogEnabled;
    fogDensity = look_.fogDensity;
    bloomStrength = look_.bloomStrength;
    hdr = look_.hdr;
    gtaoEnabled = look_.gtaoEnabled;
    ambientEnabled = look_.ambientEnabled;
    ambientIntensity = look_.ambientIntensity;
    ambientColor = look_.ambientColor;
}

// 生成一份平台跳跃模板场景：一个不可见的玩家出生点加一盏太阳光。
// 重力取 30，比地球重力（约 9.8）大约 3 倍，手感更干脆、跳跃弧线更短
bool Scene::Gvtcier_Scene_BuildPlatformer() {
    objects_.Gvtcier_Object_Clear();
    nameIndexValid_ = false;
    gravity = 30.0f;

    Object player;
    player.name = "Hero";
    player.tag = "player";
    player.kind = ObjKind::Player;
    player.pos = Vec3(0, 1.2f, 0);
    // moveSpeed 置 0 表示不覆盖行走速度，沿用物理层的 walkSpeed/runSpeed 设置
    player.moveSpeed = 0.0f;
    player.jumpHeight = 1.8f;
    player.collider = ColliderType::None;
    // 模板阶段隐藏玩家本体，由相机逻辑接管
    player.visible = false;
    assignUid(player);
    objects_.Gvtcier_Object_Register(player);

    Object light0;
    light0.name = "Sun";
    light0.kind = ObjKind::LightDir;
    // 方向光用旋转角表达朝向：俯角 52 度、方位 38 度
    light0.rotDeg = Vec3(-52.0f, 38.0f, 0.0f);
    light0.lightColor = Vec3(1.0f, 0.96f, 0.88f);
    light0.lightIntensity = 0.52f;
    assignUid(light0);
    objects_.Gvtcier_Object_Register(light0);

    return true;
}

// 判断 ancestor 是否是 id 的祖先（沿 parent 链向上走）。
// limit 用对象总数封顶，防止父子关系异常成环时死循环
bool Scene::Gvtcier_Scene_IsAncestor(GvtId<Object> ancestor, GvtId<Object> id) const {
    if (!ancestor.valid() || !id.valid()) return false;
    const Object* cur = objects_.Gvtcier_Object_Get(id);
    if (!cur) return false;
    GvtId<Object> p = cur->parent;
    int guard = 0;
    int limit = (int)objects_.Gvtcier_Object_Count() + 1;
    while (p.valid() && guard++ < limit) {
        if (p == ancestor) return true;
        const Object* po = objects_.Gvtcier_Object_Get(p);
        if (!po) return false;
        p = po->parent;
    }
    return false;
}

// 设置父子关系。拒绝三种非法情况：自引用、父对象 ID 失效、把父挂到自己的后代下。
// 建立关系时会把子对象当前的世界位姿换算成相对父的局部位姿，保证挂接瞬间位置不跳变，
// 然后把 xformDirty 与缓存脏标记一起打开，下一帧刷新世界变换
bool Scene::Gvtcier_Scene_SetParent(GvtId<Object> child, GvtId<Object> parent) {
    Object* c = objects_.Gvtcier_Object_Get(child);
    if (!c) return false;
    if (parent.valid() && !objects_.Gvtcier_Object_Get(parent)) return false;
    if (child == parent) return false;
    if (parent.valid() && Gvtcier_Scene_IsAncestor(child, parent)) return false;

    // 解除父子关系时，局部位姿归零，但世界位姿保持不变（此处没有把局部值写回世界值，
    // 只是清空局部字段，对象会留在当前世界位置）
    if (!parent.valid()) {
        if (c->parent.valid()) {
            c->localPos = Vec3(0, 0, 0);
            c->localRotDeg = Vec3(0, 0, 0);
            c->localScale = Vec3(1, 1, 1);
            c->parent = GvtId<Object>{};
        }
        return true;
    }

    const Object* p = objects_.Gvtcier_Object_Get(parent);
    // 世界位移差转到父的旋转空间，得到子相对父的局部位置。这里只做旋转不做平移，
    // 所以 localPos 表示的是“子位置减去父位置”后在父坐标系下的表达
    Mat4 pm = Quat::fromEulerDeg(p->rotDeg.x, p->rotDeg.y, p->rotDeg.z).toMat4();
    Vec3 d = c->pos - p->pos;
    Vec3 rel(d.x * pm.m[0] + d.y * pm.m[4] + d.z * pm.m[8],
             d.x * pm.m[1] + d.y * pm.m[5] + d.z * pm.m[9],
             d.x * pm.m[2] + d.y * pm.m[6] + d.z * pm.m[10]);
    // 父缩放分量可能为 0，除法前替换为 1 避免除零产生 inf
    c->localPos = Vec3(rel.x / (p->scale.x != 0.0f ? p->scale.x : 1.0f),
                       rel.y / (p->scale.y != 0.0f ? p->scale.y : 1.0f),
                       rel.z / (p->scale.z != 0.0f ? p->scale.z : 1.0f));
    c->localRotDeg = c->rotDeg - p->rotDeg;
    c->localScale = Vec3(c->scale.x / (p->scale.x != 0.0f ? p->scale.x : 1.0f),
                         c->scale.y / (p->scale.y != 0.0f ? p->scale.y : 1.0f),
                         c->scale.z / (p->scale.z != 0.0f ? p->scale.z : 1.0f));
    c->parent = parent;
    c->xformDirty = true;
    c->Gvtcier_Object_MarkXformDirty();
    return true;
}

// 把所有以 id 为父的子对象解除父子关系并清空局部位姿。
// 这里直接把 xformDirty 置假并标记缓存脏，子对象的世界位姿保持原样
void Scene::Gvtcier_Scene_DetachChildren(GvtId<Object> id) {
    if (!objects_.Gvtcier_Object_Get(id)) return;
    objects_.ForEachAlive([&](Object& o, GvtId<Object>) {
        if (o.parent != id) return;
        o.parent = GvtId<Object>{};
        o.localPos = Vec3(0, 0, 0);
        o.localRotDeg = Vec3(0, 0, 0);
        o.localScale = Vec3(1, 1, 1);
        o.xformDirty = false;
        o.Gvtcier_Object_MarkCacheDirty();
    });
}

namespace {

// 向上递归解析一个对象的层级，把局部位姿累乘成世界位姿（就地写入 pos/rotDeg/scale）。
// 返回 true 表示本次确实重算了该对象。三个关键行为：
// 1) 父对象丢失（ID 失效）时把对象提升为根，清掉悬空父引用；
// 2) 子对象自身不脏且父对象也没重算，直接跳过，避免整棵树每帧重算；
// 3) staticXform 的对象在父未变化时跳过重算，作为静态优化
bool resolveOne(Gvtcier_Object<Object>& store, GvtId<Object> id, int depth, int limit) {
    Object* c = store.Gvtcier_Object_Get(id);
    if (!c || depth > limit) return false;
    Object* p = c->parent.valid() ? store.Gvtcier_Object_Get(c->parent) : nullptr;
    if (!p) {
        c->parent = GvtId<Object>{};
        bool wasDirty = c->xformDirty;
        c->xformDirty = false;
        return wasDirty;
    }
    bool parentChanged = resolveOne(store, c->parent, depth + 1, limit);
    if (!c->xformDirty && !parentChanged) return false;
    if (c->staticXform && !parentChanged) {
        c->xformDirty = false;
        return false;
    }
    // 先按父缩放缩放局部位置，再转到父旋转空间，最后叠加父位置得到世界坐标
    Mat4 pm = Quat::fromEulerDeg(p->rotDeg.x, p->rotDeg.y, p->rotDeg.z).toMat4();
    Vec3 ls = Vec3(c->localPos.x * p->scale.x, c->localPos.y * p->scale.y, c->localPos.z * p->scale.z);
    Vec3 w(ls.x * pm.m[0] + ls.y * pm.m[4] + ls.z * pm.m[8],
           ls.x * pm.m[1] + ls.y * pm.m[5] + ls.z * pm.m[9],
           ls.x * pm.m[2] + ls.y * pm.m[6] + ls.z * pm.m[10]);
    c->pos = p->pos + w;
    // 欧拉角直接相加只是近似合成，父与子同时有大角度旋转时会与真实四元数复合有偏差
    c->rotDeg = p->rotDeg + c->localRotDeg;
    c->scale = Vec3(c->localScale.x * p->scale.x, c->localScale.y * p->scale.y, c->localScale.z * p->scale.z);
    c->xformDirty = false;
    c->Gvtcier_Object_MarkCacheDirty();
    return true;
}

}

// 刷新整个场景的世界变换。分两步：先把“脏对象”的所有子对象也标脏（脏向下传播一层，
// 再依靠 resolveOne 的递归把整条链补全），然后对所有有父对象的对象调用 resolveOne。
// 注意 roots 收集的是有父对象的对象，全部走一遍会重复计算部分链，但能保证顺序无关
void Scene::Gvtcier_Scene_RefreshTransforms() {
    std::vector<GvtId<Object>> roots;
    objects_.ForEachAlive([&](Object& o, GvtId<Object> id) {
        if (o.xformDirty) {
            objects_.ForEachAlive([&](Object& c, GvtId<Object>) {
                if (c.parent == id) c.xformDirty = true;
            });
        }
    });
    int limit = (int)objects_.Gvtcier_Object_Count() + 1;
    objects_.ForEachAlive([&](Object& o, GvtId<Object> id) {
        if (!o.parent.valid()) return;
        roots.push_back(id);
    });
    for (GvtId<Object> id : roots) resolveOne(objects_, id, 0, limit);
}

// 真正执行对象销毁。延迟删除的原因是：遍历中直接删会破坏迭代，
// 所以先收集到 pendingRemoves，帧末统一处理。顺序很重要：
// 先通知物理层移除刚体，再把被删对象的子对象提升为根，最后才 Destroy，
// 这样子对象不会留下指向已销毁父的悬空 ID
void Scene::Gvtcier_Scene_FlushRemoves(PhysicsWorld* physics) {
    if (pendingRemoves.empty()) return;
    std::vector<GvtId<Object>> unique;
    unique.reserve(pendingRemoves.size());
    // 去重：同一对象可能在一帧内被请求删除多次，重复销毁会让世代号多增
    for (GvtId<Object> id : pendingRemoves) {
        if (!id.valid()) continue;
        bool dup = false;
        for (GvtId<Object> u : unique) if (u == id) { dup = true; break; }
        if (!dup) unique.push_back(id);
    }
    if (physics) physics->Gvtcier_Physics_RemoveObjects(unique);

    for (GvtId<Object> id : unique) {
        objects_.ForEachAlive([&](Object& o, GvtId<Object>) {
            if (o.parent == id) {
                o.parent = GvtId<Object>{};
                o.localPos = Vec3(0, 0, 0);
                o.localRotDeg = Vec3(0, 0, 0);
                o.localScale = Vec3(1, 1, 1);
                o.xformDirty = false;
            }
        });
    }
    for (GvtId<Object> id : unique) objects_.Gvtcier_Object_Destroy(id);
    pendingRemoves.clear();
    Gvtcier_Scene_InvalidateNameIndex();
}

// 清空所有对象及其待删队列。不做逐个销毁的善后（如通知物理层），
// 调用方需自行保证物理世界也会被整体重建
void Scene::Gvtcier_Scene_ClearObjects() {
    objects_.Gvtcier_Object_Clear();
    pendingRemoves.clear();
    uidIndex_.clear();
    uidIndexValid_ = false;
    Gvtcier_Scene_InvalidateNameIndex();
}

// 名称索引失效：任何增删改名的操作都必须调用，否则会查到过期 ID
void Scene::Gvtcier_Scene_InvalidateNameIndex() {
    nameIndexValid_ = false;
}

// 按名字查对象。索引采用惰性重建：首次查询时全量扫描一次，
// 重名时 emplace 只保留先出现的那个，后添加的同名对象查不到
GvtId<Object> Scene::Gvtcier_Scene_FindByName(const char* name) {
    if (!name) return GvtId<Object>{};
    if (!nameIndexValid_) {
        nameIndex_.clear();
        objects_.ForEachAlive([&](const Object& o, GvtId<Object> id) {
            if (!o.name.empty()) nameIndex_.emplace(o.name, id);
        });
        nameIndexValid_ = true;
    }
    std::unordered_map<std::string, GvtId<Object>>::const_iterator it = nameIndex_.find(name);
    if (it == nameIndex_.end()) return GvtId<Object>{};
    return it->second;
}

// 从资源加载模型网格。格式判定：前 4 字节是 glTF 魔数 0x46546C67（小端 "glTF"）
// 或首字符是 '{'（GLTF 的 JSON 文本版）都走 GLTF 分支，否则按 OBJ 解析。
// 返回的是共享的 obj/gltf 网格槽位 ID，多次加载会互相覆盖
GvtId<Mesh> Scene::Gvtcier_Scene_LoadModel(const char* resName) {
    std::vector<unsigned char> bytes;
    if (!readResBytes(resName, bytes) || bytes.empty()) return GvtId<Mesh>{};
    const unsigned char* d = bytes.data();
    size_t len = bytes.size();
    bool gltf = len >= 4 && (*(const unsigned*)d == 0x46546C67u || d[0] == '{');
    if (gltf) return loadGltfMesh(d, len, meshes_->gltfMesh_) ? gltfMeshId_ : GvtId<Mesh>{};
    return loadObjMesh(d, len, meshes_->objMesh_) ? objMeshId_ : GvtId<Mesh>{};
}

// 从普通文件加载模型，写进共享的 fileMesh_ 槽位。
// 256 MB 上限用于拦住误传的巨型文件，避免一次性占满内存
GvtId<Mesh> Scene::Gvtcier_Scene_LoadModelFile(const char* path) {
    if (!path || !path[0]) return GvtId<Mesh>{};
    std::vector<unsigned char> data;
    if (!Gvtcier_Res_ReadFile(path, data)) return GvtId<Mesh>{};
    if (data.empty() || data.size() > 256 * 1024 * 1024) return GvtId<Mesh>{};
    size_t len = data.size();
    const unsigned char* d = data.data();
    bool gltf = len >= 4 && (*(const unsigned*)d == 0x46546C67u || d[0] == '{');
    if (gltf) return loadGltfMesh(d, (size_t)len, meshes_->fileMesh_) ? fileMeshId_ : GvtId<Mesh>{};
    return loadObjMesh(d, (size_t)len, meshes_->fileMesh_) ? fileMeshId_ : GvtId<Mesh>{};
}

// 按路径加载“独立”模型：先查缓存，命中直接复用；否则解析到 fileMesh_ 后把网格内容
// 整体移动注册成一个新网格，这样每个路径拥有自己的网格，不会互相覆盖。
// 注意移动之后 fileMesh_ 被搬空，源槽位不再可用
GvtId<Mesh> Scene::Gvtcier_Scene_LoadModelObject(const char* path) {
    if (!path || !path[0]) return GvtId<Mesh>{};
    auto it = meshes_->modelMeshs_.find(path);
    if (it != meshes_->modelMeshs_.end()) return it->second;
    GvtId<Mesh> m = Gvtcier_Scene_LoadModelFile(path);
    if (!m.valid()) return GvtId<Mesh>{};
    Mesh* src = meshStore_.Gvtcier_Object_Get(m);
    if (!src) return GvtId<Mesh>{};
    GvtId<Mesh> id = meshStore_.Gvtcier_Object_Register(std::move(*src));
    meshes_->modelMeshs_[path] = id;
    return id;
}

// 载入纯文本地图 gmap 并转换成一整批场景对象。这是关卡进入场景的唯一入口。
//
// gmap 文档由若干块组成，每块形如 “块名 { 字段 值... }”：
//   世界 { 天空 r g b / 环境光 r g b [强度] / 太阳 方向 x y z / 太阳颜色 r g b / 雾 密度 / 重力 g }
//   画刷 { 名称 位置 x y z 尺寸 x y z 贴图 自发光 旋转 x y z 碰撞 盒|网格|球|胶囊|圆柱|无 ... }
//   灯光 { 种类 点|聚|方向 位置 颜色 强度 范围 方向 角度 阴影 }
//   点实体 { 类别 特效|音频|图片|文本|模型|相机 ... }
//   玩家 { 位置 速度 跳跃 视场角 近平面 远平面 }
//   物品 { 名称 位置 模型 ... }
// 具体字段名与默认值由 gmap::GMapDoc::loadMemory 负责，本函数只消费解析结果。
//
// 语义说明：
// - “世界”块的天空/环境光/雾/重力/太阳写进 Scene 自身的全局字段，影响整个场景的渲染与物理；
//   其中 ambientEnabled 被无条件置真，即只要地图带环境光设置就一定启用环境光。
// - “画刷”是静态几何体，全部以 cubeMesh 为底网格，通过 scale 表示盒体尺寸；
//   碰撞体类型决定物理层用哪种形状，球形与柱形会换成对应的渲染网格。
// - “灯光”“点实体”“玩家”“物品”各自映射成不同 ObjKind 的对象。
// 函数本身不清理旧对象，重复调用会叠加，需要清场景请先调用 ClearObjects。
bool Scene::Gvtcier_Scene_LoadGmap(const char* gmapPath) {
    if (!gmapPath || !gmapPath[0]) return false;
    std::vector<unsigned char> gm;
    // 先当普通文件读，失败再尝试从打包资源表里取（资源目录里存的地图走这条路径）
    if (!Gvtcier_Res_ReadFile(gmapPath, gm) || gm.empty()) {
        int sz = 0;
        const void* p = res_get(gmapPath, &sz);
        if (!p || sz <= 0) return false;
        gm.assign((const unsigned char*)p, (const unsigned char*)p + sz);
    }
    gmap::GMapDoc doc;
    if (!doc.loadMemory((const char*)gm.data(), gm.size())) return false;

    // “世界”块：全局渲染与物理参数。天蓝色 0.62/0.78/0.96 与默认天空一致；
    // 重力是正数标量，Scene.gravity 约定为“向下的加速度大小”，物理层初始化时再取负号
    skyColor = Vec3(doc.world.sky.x, doc.world.sky.y, doc.world.sky.z);
    ambientColor = Vec3(doc.world.ambient.x, doc.world.ambient.y, doc.world.ambient.z);
    ambientIntensity = doc.world.ambientIntensity;
    ambientEnabled = true;
    fogDensity = doc.world.fog;
    gravity = doc.world.gravity;
    // 太阳仅在文档里显式启用时才覆盖，否则保留此前 InitSun 设定的默认方向与颜色
    if (doc.world.sunEnabled) {
        light.dir = Vec3(doc.world.sunDir.x, doc.world.sunDir.y, doc.world.sunDir.z);
        light.color = Vec3(doc.world.sunColor.x, doc.world.sunColor.y, doc.world.sunColor.z);
        sunEnabled = true;
    }

    // “画刷”块：静态几何。位置是盒体中心，尺寸是盒体全长（不是半长），
    // 因为 cubeMesh 是单位盒，scale 直接等于全长即可得到对应尺寸
    for (const gmap::BrushDef& b : doc.brushes) {
        Object o;
        o.name = b.name;
        o.uid = b.uid;
        o.tag = b.tag;
        o.kind = ObjKind::Geometry;
        o.mesh = cubeMeshId_;
        o.tex = whiteTexId_;
        o.pos = Vec3(b.boxCenter.x, b.boxCenter.y, b.boxCenter.z);
        o.scale = Vec3(b.boxSize.x, b.boxSize.y, b.boxSize.z);
        o.material = b.material;
        // 自发光画刷统一使用材质编号 5，渲染层据此走发光着色分支
        if (b.emissive) o.material = 5;
        o.specular = b.specular;
        o.uvScale = b.uvScale;
        o.visible = b.visible;
        o.transparent = b.transparent;
        o.rigidBody = b.rigid;
        o.physics = b.physics;
        o.kinematic = b.kinematic;
        o.pushable = b.pushable;
        o.mass = b.mass;
        o.friction = b.friction;
        o.scriptPath = b.script;
        // 贴图字段是资源名，交给 LoadPngRes 查找；解析失败时保持白色占位。
        // texName 始终记录原始名字，方便编辑器面板回显
        if (!b.tex.empty()) {
            GvtId<Texture> tid = Gvtcier_Scene_LoadPngRes(b.tex.c_str());
            if (tid.valid()) o.tex = tid;
            o.texName = b.tex;
        } else {
            o.tex = whiteTexId_;
        }
        // “碰撞”字段是中文枚举：缺省视为盒体；“无”表示不参与碰撞（物理层会跳过）
        if (b.collider.empty() || b.collider == "盒") o.collider = ColliderType::Box;
        else if (b.collider == "网格") o.collider = ColliderType::Mesh;
        else if (b.collider == "球") o.collider = ColliderType::Sphere;
        else if (b.collider == "胶囊") o.collider = ColliderType::Capsule;
        else if (b.collider == "圆柱") o.collider = ColliderType::Cylinder;
        else if (b.collider == "无") o.collider = ColliderType::None;
        // 球与柱/胶囊的渲染网格换成对应形状，否则玩家看到的是盒子而碰撞是圆的
        if (o.collider == ColliderType::Sphere) o.mesh = sphereMeshId_;
        else if (o.collider == ColliderType::Cylinder || o.collider == ColliderType::Capsule) o.mesh = cylinderMeshId_;
        // 触发器画刷：不可见但保留盒体碰撞，供脚本用重叠检测判定进入/离开
        if (b.kind == gmap::BrushKind::Trigger) {
            o.visible = false;
            o.collider = ColliderType::Box;
        }
        Gvtcier_Scene_AddObject(o);
    }

    // “灯光”块：位置 + 颜色 + 强度 + 范围，聚光灯额外有方向与锥角。
    // 方向光不存在真正的“位置”，这里的 pos 只是编辑器里的方向指示点；
    // 方向光朝向沿用 rotDeg 字段（与 BuildPlatformer 里的约定一致）
    for (const gmap::LightDef& l : doc.lights) {
        Object o;
        o.name = l.name;
        o.tag = l.tag;
        o.kind = l.type == gmap::LightType::Spot ? ObjKind::LightSpot
               : l.type == gmap::LightType::Dir ? ObjKind::LightDir : ObjKind::LightPoint;
        o.pos = Vec3(l.pos.x, l.pos.y, l.pos.z);
        o.rotDeg = Vec3(l.dir.x, l.dir.y, l.dir.z);
        o.lightColor = Vec3(l.color.x, l.color.y, l.color.z);
        o.lightIntensity = l.intensity;
        o.lightRange = l.range;
        o.spotAngle = l.angle;
        o.lightShadow = l.shadow;
        o.scriptPath = l.script;
        Gvtcier_Scene_AddObject(o);
    }

    // “点实体”块：按类别分发到不同 ObjKind，同一份 asset 字段在不同类别下含义不同
    // （特效是特效资源名、音频是音频文件、图片是贴图名、文本是正文、模型是模型路径）
    for (const gmap::PointEntityDef& p : doc.pointEntities) {
        Object o;
        o.name = p.name;
        o.tag = p.tag;
        o.pos = Vec3(p.pos.x, p.pos.y, p.pos.z);
        o.rotDeg = Vec3(p.rotDeg.x, p.rotDeg.y, p.rotDeg.z);
        o.scriptPath = p.script;
        switch (p.cls) {
            case gmap::PointClass::Effect:
                o.kind = ObjKind::Effect;
                o.effectAsset = p.asset;
                o.effectParam0 = p.param0;
                o.effectParam1 = p.param1;
                o.effectDir = p.dir;
                o.effectEmissive = p.emissive;
                break;
            case gmap::PointClass::Audio:
                o.kind = ObjKind::Audio;
                o.audioPath = p.asset;
                o.volume = p.volume;
                o.soundRange = p.soundRange;
                o.pitch = p.pitch;
                o.loop = p.loop;
                o.autoPlay = p.autoplay;
                break;
            case gmap::PointClass::Image: {
                o.kind = ObjKind::Image;
                o.imageName = p.asset;
                if (!p.asset.empty()) {
                    GvtId<Texture> it = Gvtcier_Scene_LoadPngRes(p.asset.c_str());
                    if (it.valid()) o.tex = it;
                }
                if (!o.tex.valid()) o.tex = whiteTexId_;
                // 图片使用朝向相机的平面网格
                o.mesh = imageMeshId_;
                // size 为图片高度；宽度按贴图宽高比算，避免图片被拉伸变形。
                // size<=0 时退化为 1.0 的默认高度
                float s = p.size > 0.0 ? (float)p.size : 1.0f;
                float ar = 1.0f;
                Texture* itx = texStore_.Gvtcier_Object_Get(o.tex);
                if (itx && itx->height() > 0) ar = (float)itx->width() / (float)itx->height();
                o.scale = Vec3(s * ar, s, 1.0f);
                // dir 非零时视为绕 Y 轴的朝向角（度）
                if (p.dir != 0.0) o.rotDeg = Vec3(0.0f, (float)p.dir, 0.0f);
                // 图片是纯平面，投影阴影没有意义，直接关掉
                o.castShadow = false;
                break;
            }
            case gmap::PointClass::Text:
                o.kind = ObjKind::Text;
                o.text = p.text;
                o.textSize = p.size;
                break;
            case gmap::PointClass::Model:
                o.kind = ObjKind::Model;
                o.modelName = p.asset;
                break;
            case gmap::PointClass::Camera:
                o.kind = ObjKind::Camera;
                // 视场角缺省 60 度，与常见游戏默认 FOV 一致
                o.fov = p.fov > 0.0f ? p.fov : 60.0f;
                break;
            default:
                o.kind = ObjKind::Geometry;
                break;
        }
        Gvtcier_Scene_AddObject(o);
    }

    // “玩家”块：出生点与操控参数。pos 是脚底位置，物理层初始化时会把胶囊中心
    // 上抬半个身高；胶囊碰撞 + 开启物理 才能被物理层当作角色驱动。
    // 渲染网格用球、缩放 0.7，只是给编辑器一个可见的占位体
    for (const gmap::PlayerDef& pl : doc.players) {
        Object o;
        o.name = "player";
        o.kind = ObjKind::Player;
        o.pos = Vec3(pl.pos.x, pl.pos.y, pl.pos.z);
        o.moveSpeed = pl.speed;
        o.jumpHeight = pl.jump;
        o.fov = pl.fov;
        o.nearPlane = pl.nearPlane;
        o.farPlane = pl.farPlane;
        o.collider = ColliderType::Capsule;
        o.physics = true;
        o.mesh = sphereMeshId_;
        o.scale = Vec3(0.7f, 0.7f, 0.7f);
        o.visible = true;
        o.scriptPath = pl.script;
        Gvtcier_Scene_AddObject(o);
    }

    // “物品”块：可拾取的模型实体，本身只有名字、位置与模型路径，拾取逻辑交给脚本
    for (const gmap::ItemDef& it : doc.items) {
        Object o;
        o.name = it.name;
        o.tag = it.tag;
        o.kind = ObjKind::Model;
        o.pos = Vec3(it.pos.x, it.pos.y, it.pos.z);
        o.modelName = it.model;
        o.scriptPath = it.script;
        Gvtcier_Scene_AddObject(o);
    }
    return true;
}

// 从打包资源加载蒙皮模型（GLTF）。加载成功后建立骨骼矩阵数组，
// 初始每根骨骼都是“单位旋转 + 平移到 pos”的矩阵，避免首帧出现未初始化矩阵。
// 与 LoadSkinnedFile 不同，这里不重建基础色纹理（skinnedTexId_ 保持原值）
bool Scene::Gvtcier_Scene_LoadSkinnedModel(const char* resName, const Vec3& pos) {
    std::vector<unsigned char> bytes;
    if (!readResBytes(resName, bytes) || bytes.empty()) return false;
    if (!loadSkinnedMesh(bytes.data(), bytes.size(), skinned_->skinnedMesh_)) return false;
    size_t n = skinned_->skinnedMesh_.bones.size();
    skinnedBoneMats.assign(n * 16, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        float* m = &skinnedBoneMats[i * 16];
        // 列主序矩阵：对角线为 1 表示单位变换，第 12/13/14 个元素是平移
        m[0] = m[5] = m[10] = m[15] = 1.0f;
        m[3] = pos.x; m[7] = pos.y; m[11] = pos.z;
    }
    return true;
}

// 从文件加载蒙皮模型，与上面不同之处：会重建基础色（albedo）纹理、
// 重置动画状态并通过一次零步长 Tick 立即生成初始姿态。
// 旧的蒙皮纹理先显式销毁，否则重新加载模型会泄漏 GPU 纹理
bool Scene::Gvtcier_Scene_LoadSkinnedFile(const char* path, const Vec3& pos) {
    if (!path || !path[0]) return false;
    std::vector<unsigned char> data;
    if (!Gvtcier_Res_ReadFile(path, data)) return false;
    // 与普通模型相同的 256 MB 体量上限
    if (data.empty() || data.size() > 256 * 1024 * 1024) return false;
    if (!loadSkinnedMesh(data.data(), data.size(), skinned_->skinnedMesh_)) return false;
    if (skinnedTexId_.valid()) texStore_.Gvtcier_Object_Destroy(skinnedTexId_);
    skinnedTexId_ = GvtId<Texture>{};
    if (skinned_->skinnedMesh_.hasBaseColorTex && skinned_->skinnedMesh_.baseColorW > 0 && skinned_->skinnedMesh_.baseColorH > 0) {
        Texture t;
        if (t.create(skinned_->skinnedMesh_.baseColorW, skinned_->skinnedMesh_.baseColorH,
                     skinned_->skinnedMesh_.baseColorPixels.data())) {
            skinnedTexId_ = texStore_.Gvtcier_Object_Register(std::move(t));
        }
    }
    // 换模型后旧动画索引可能越界，全部重置；animPoseDirty 保证下面那次 Tick 会重采样
    animIndex_ = -1;
    animPlaying_ = false;
    animTime_ = 0.0f;
    animPoseDirty_ = true;
    skinnedPos_ = pos;
    Gvtcier_Scene_AnimTick(0.0f);
    return true;
}

int Scene::Gvtcier_Scene_AnimCount() const { return (int)skinned_->skinnedMesh_.anims.size(); }

// 越界索引返回空串，调用方可以直接显示而不必判空
const char* Scene::Gvtcier_Scene_AnimName(int i) const {
    if (i < 0 || i >= (int)skinned_->skinnedMesh_.anims.size()) return "";
    return skinned_->skinnedMesh_.anims[i].name.c_str();
}

float Scene::Gvtcier_Scene_AnimDuration(int i) const {
    if (i < 0 || i >= (int)skinned_->skinnedMesh_.anims.size()) return 0.0f;
    return skinned_->skinnedMesh_.anims[i].duration;
}

// 播放第 i 段动画。循环播放时会按总时长取模；非循环在末尾停住并保持最后一帧
bool Scene::Gvtcier_Scene_AnimPlay(int i, bool loop) {
    if (i < 0 || i >= (int)skinned_->skinnedMesh_.anims.size()) return false;
    animIndex_ = i;
    animPlaying_ = true;
    animLoop_ = loop;
    animTime_ = 0.0f;
    animPoseDirty_ = true;
    return true;
}

// 只停止时间推进，不改 animTime_，所以再播放会从当前帧继续
void Scene::Gvtcier_Scene_AnimStop() {
    animPlaying_ = false;
}

bool Scene::Gvtcier_Scene_AnimPlaying() const { return animPlaying_; }
void Scene::Gvtcier_Scene_AnimSetSpeed(float s) { animSpeed_ = s; }
float Scene::Gvtcier_Scene_AnimSpeed() const { return animSpeed_; }
// 手动定位到某一时刻，需要把姿态标脏才会在下次 Tick 重新采样
void Scene::Gvtcier_Scene_AnimSetTime(float t) { animTime_ = t; animPoseDirty_ = true; }
float Scene::Gvtcier_Scene_AnimTime() const { return animTime_; }
void Scene::Gvtcier_Scene_AnimSetPos(const Vec3& p) { skinnedPos_ = p; animPoseDirty_ = true; }
Vec3 Scene::Gvtcier_Scene_AnimPos() const { return skinnedPos_; }

// 每帧推进蒙皮动画。没有骨骼时直接返回。
// 时间推进只在“正在播放”时进行；循环用 fmod 取模并修正负数（速度为负时倒放），
// 非循环越过时长就夹到末尾并停止。真正开销大的是后面的采样与骨骼矩阵合成，
// 因此用 animPoseDirty_ 把两者合并：姿态没变就整段跳过
void Scene::Gvtcier_Scene_AnimTick(float dt) {
    if (skinned_->skinnedMesh_.bones.empty()) return;
    if (animPlaying_ && animIndex_ >= 0 && animIndex_ < (int)skinned_->skinnedMesh_.anims.size()) {
        float dur = skinned_->skinnedMesh_.anims[animIndex_].duration;
        // 时长过小的动画视为无效，避免取模除零
        if (dur > 1e-6f) {
            animTime_ += dt * animSpeed_;
            if (animLoop_) {
                animTime_ = std::fmod(animTime_, dur);
                if (animTime_ < 0.0f) animTime_ += dur;
            } else if (animTime_ > dur) {
                animTime_ = dur;
                animPlaying_ = false;
            }
            animPoseDirty_ = true;
        }
    }
    if (!animPoseDirty_) return;
    animPoseDirty_ = false;
    sampleSkinnedAnim(skinned_->skinnedMesh_, animIndex_, animTime_, skinned_->skinnedMesh_.pose);
    // 根节点的世界矩阵由模型自带的 rootTransform 平移到当前蒙皮位置得到，
    // 平移直接加在矩阵的 12/13/14 元素上（前提是 rootTransform 无旋转缩放，实际一般成立）
    Mat4 root = skinned_->skinnedMesh_.rootTransform;
    root.m[12] += skinnedPos_.x;
    root.m[13] += skinnedPos_.y;
    root.m[14] += skinnedPos_.z;
    computeSkinnedBoneMats(skinned_->skinnedMesh_, skinned_->skinnedMesh_.pose, root, skinnedBoneMats);
}

// 把归一化进度 t 映射成缓动后的进度。Step 恒返回 1，即“立刻跳到终点”
static float animEaseApply(AnimEase e, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    switch (e) {
        case AnimEase::Linear: return t;
        case AnimEase::EaseIn: return t * t;
        case AnimEase::EaseOut: return 1.0f - (1.0f - t) * (1.0f - t);
        case AnimEase::EaseInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
        case AnimEase::Step: return 1.0f;
        default: return t;
    }
}

// 读取对象的可动画属性。key 用中文属性名，“位置/旋转/缩放”不带后缀时按 comp 选分量，
// 带 X/Y/Z 后缀时忽略 comp 直接取对应分量
static bool animPropGet(Object* o, const char* key, int comp, double* out) {
    if (!o || !key || !out) return false;
    if (comp < 0 || comp > 2) comp = 0;
    if (strcmp(key, "位置") == 0) { *out = comp == 0 ? o->pos.x : comp == 1 ? o->pos.y : o->pos.z; return true; }
    if (strcmp(key, "位置X") == 0) { *out = o->pos.x; return true; }
    if (strcmp(key, "位置Y") == 0) { *out = o->pos.y; return true; }
    if (strcmp(key, "位置Z") == 0) { *out = o->pos.z; return true; }
    // 注意旋转只有不带后缀的版本，无法单独动画某一个轴
    if (strcmp(key, "旋转") == 0) { *out = comp == 0 ? o->rotDeg.x : comp == 1 ? o->rotDeg.y : o->rotDeg.z; return true; }
    if (strcmp(key, "缩放") == 0) { *out = comp == 0 ? o->scale.x : comp == 1 ? o->scale.y : o->scale.z; return true; }
    if (strcmp(key, "缩放X") == 0) { *out = o->scale.x; return true; }
    if (strcmp(key, "缩放Y") == 0) { *out = o->scale.y; return true; }
    if (strcmp(key, "缩放Z") == 0) { *out = o->scale.z; return true; }
    if (strcmp(key, "颜色R") == 0) { *out = o->albedoTint.x; return true; }
    if (strcmp(key, "颜色G") == 0) { *out = o->albedoTint.y; return true; }
    if (strcmp(key, "颜色B") == 0) { *out = o->albedoTint.z; return true; }
    if (strcmp(key, "光照强度") == 0) { *out = o->lightIntensity; return true; }
    if (strcmp(key, "光照范围") == 0) { *out = o->lightRange; return true; }
    if (strcmp(key, "高光") == 0) { *out = o->shininess; return true; }
    if (strcmp(key, "纹理平铺") == 0) { *out = o->uvScale; return true; }
    if (strcmp(key, "透明度") == 0) { *out = o->alpha; return true; }
    return false;
}

// 写入对象可动画属性。凡是改到位姿的键都必须调 MarkXformDirty，
// 否则世界矩阵缓存与世界变换刷新都不会察觉到变化
static bool animPropSet(Object* o, const char* key, int comp, double v) {
    if (!o || !key) return false;
    if (comp < 0 || comp > 2) comp = 0;
    float f = (float)v;
    if (strcmp(key, "位置") == 0) { if (comp == 0) o->pos.x = f; else if (comp == 1) o->pos.y = f; else o->pos.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "位置X") == 0) { o->pos.x = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "位置Y") == 0) { o->pos.y = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "位置Z") == 0) { o->pos.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "旋转") == 0) { if (comp == 0) o->rotDeg.x = f; else if (comp == 1) o->rotDeg.y = f; else o->rotDeg.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放") == 0) { if (comp == 0) o->scale.x = f; else if (comp == 1) o->scale.y = f; else o->scale.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放X") == 0) { o->scale.x = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放Y") == 0) { o->scale.y = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "缩放Z") == 0) { o->scale.z = f; o->Gvtcier_Object_MarkXformDirty(); return true; }
    if (strcmp(key, "颜色R") == 0) { o->albedoTint.x = f; return true; }
    if (strcmp(key, "颜色G") == 0) { o->albedoTint.y = f; return true; }
    if (strcmp(key, "颜色B") == 0) { o->albedoTint.z = f; return true; }
    if (strcmp(key, "光照强度") == 0) { o->lightIntensity = f; return true; }
    if (strcmp(key, "光照范围") == 0) { o->lightRange = f; return true; }
    if (strcmp(key, "高光") == 0) { o->shininess = f; return true; }
    if (strcmp(key, "纹理平铺") == 0) { o->uvScale = f; return true; }
    // 透明度夹在 0..1，并顺带维护 transparent 标志，让渲染层自动切到混合管线
    if (strcmp(key, "透明度") == 0) {
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        o->alpha = f;
        o->transparent = f < 1.0f;
        return true;
    }
    return false;
}

// 创建一个动画状态机并返回下标（不是 GvtId）。状态机只存在于场景内部的
// animSms_ 数组里，删除状态机没有对应接口，数组下标在增删后可能失效
int Scene::Gvtcier_Scene_AnimSmCreate(const char* name, GvtId<Object> target) {
    AnimStateMachine sm;
    sm.name = name ? name : "";
    sm.target = target;
    animSms_.push_back(std::move(sm));
    return (int)animSms_.size() - 1;
}

int Scene::Gvtcier_Scene_AnimSmFind(const char* name) const {
    if (!name) return -1;
    for (size_t i = 0; i < animSms_.size(); ++i) {
        if (animSms_[i].name == name) return (int)i;
    }
    return -1;
}

// 越界返回 nullptr，调用方必须判空
AnimStateMachine* Scene::Gvtcier_Scene_AnimSm(int id) {
    if (id < 0 || id >= (int)animSms_.size()) return nullptr;
    return &animSms_[id];
}

int Scene::Gvtcier_Scene_AnimSmStateId(AnimStateMachine* sm, const char* stateName) {
    if (!sm || !stateName) return -1;
    for (size_t i = 0; i < sm->states.size(); ++i) {
        if (sm->states[i].name == stateName) return (int)i;
    }
    return -1;
}

// 添加状态。已存在同名状态则不重复添加；hold 是进入该状态后的保持时长
void Scene::Gvtcier_Scene_AnimSmAddState(int id, const char* stateName, bool loop, float hold) {
    AnimStateMachine* sm = Gvtcier_Scene_AnimSm(id);
    if (!sm || !stateName) return;
    if (Gvtcier_Scene_AnimSmStateId(sm, stateName) >= 0) return;
    AnimState st;
    st.name = stateName;
    st.loop = loop;
    st.hold = hold;
    sm->states.push_back(std::move(st));
}

// 给状态添加一条属性轨道：在 dur 秒内把 key 的第 comp 个分量从当前值过渡到 to。
// 状态不存在时会用状态机的 loopDefault 自动补建该状态
void Scene::Gvtcier_Scene_AnimSmAddTrack(int id, const char* stateName, const char* key, int comp, double to, float dur, AnimEase ease) {
    AnimStateMachine* sm = Gvtcier_Scene_AnimSm(id);
    if (!sm || !stateName || !key) return;
    int si = Gvtcier_Scene_AnimSmStateId(sm, stateName);
    if (si < 0) {
        AnimState st;
        st.name = stateName;
        st.loop = sm->loopDefault;
        sm->states.push_back(std::move(st));
        si = (int)sm->states.size() - 1;
    }
    AnimTrack tr;
    tr.key = key;
    tr.comp = comp;
    tr.to = to;
    // 负的时长按 0 处理，即瞬时跳变
    tr.dur = dur > 0.0f ? dur : 0.0f;
    tr.ease = ease;
    sm->states[si].tracks.push_back(std::move(tr));
}

// 切换状态。immediate 表示允许零时长轨道当场吸附到目标值（不等下一帧），
// 同时不写入 queued；非 immediate 时把状态下标放进 queued，由下一次 Tick 再走一遍切换流程。
// 已是当前状态时直接返回成功，不重播
bool Scene::Gvtcier_Scene_AnimSmSet(int id, const char* stateName, bool immediate) {
    AnimStateMachine* sm = Gvtcier_Scene_AnimSm(id);
    if (!sm || !stateName) return false;
    int si = Gvtcier_Scene_AnimSmStateId(sm, stateName);
    if (si < 0) return false;
    if (sm->current == si) return true;
    sm->queued = immediate ? -1 : si;
    sm->current = si;
    sm->playing = true;
    Object* o = Gvtcier_Scene_GetObject(sm->target);
    AnimState& st = sm->states[si];
    for (size_t k = 0; k < st.tracks.size(); ++k) {
        AnimTrack& tr = st.tracks[k];
        // 先用目标值当缺省，取不到当前属性值时就从目标值起步（等于不动）
        double from = tr.to;
        if (o) animPropGet(o, tr.key.c_str(), tr.comp, &from);
        tr.from = from;
        tr.cur = 0.0f;
        tr.started = true;
        if (immediate && tr.dur <= 0.0f && o) animPropSet(o, tr.key.c_str(), tr.comp, tr.to);
    }
    return true;
}

void Scene::Gvtcier_Scene_AnimSmSetSpeed(int id, float speed) {
    AnimStateMachine* sm = Gvtcier_Scene_AnimSm(id);
    if (sm) sm->speed = speed;
}

// 返回当前状态名的裸指针，指向状态机内部字符串；
// 下次添加状态导致 vector 扩容时该指针可能失效
const char* Scene::Gvtcier_Scene_AnimSmCurrent(int id) const {
    if (id < 0 || id >= (int)animSms_.size()) return "";
    const AnimStateMachine& sm = animSms_[id];
    if (sm.current < 0 || sm.current >= (int)sm.states.size()) return "";
    return sm.states[sm.current].name.c_str();
}

// 每帧推进所有状态机的属性轨道。核心是按 dur 归一化得到 t，经缓动函数映射后再线性插值
// from→to；所有轨道都跑完时，循环状态重置回起点、非循环状态停止播放
void Scene::Gvtcier_Scene_AnimSmTick(float dt) {
    for (size_t i = 0; i < animSms_.size(); ++i) {
        AnimStateMachine& sm = animSms_[i];
        if (!sm.playing) continue;
        // 处理排队切换：queued 存的是待切换状态下标，取走后立即清空避免重复执行
        if (sm.queued >= 0) {
            int q = sm.queued;
            sm.queued = -1;
            Gvtcier_Scene_AnimSmSet((int)i, sm.states[q].name.c_str(), false);
        }
        if (sm.current < 0 || sm.current >= (int)sm.states.size()) continue;
        Object* o = Gvtcier_Scene_GetObject(sm.target);
        if (!o) continue;
        AnimState& st = sm.states[sm.current];
        bool allDone = true;
        float step = dt * sm.speed;
        for (size_t k = 0; k < st.tracks.size(); ++k) {
            AnimTrack& tr = st.tracks[k];
            // 第一次执行该轨道时才记录起点；此后 from 保持不变，避免逐帧漂移
            if (!tr.started) {
                double from = tr.to;
                animPropGet(o, tr.key.c_str(), tr.comp, &from);
                tr.from = from;
                tr.cur = 0.0f;
                tr.started = true;
            }
            if (tr.dur > 0.0f) {
                tr.cur += step;
                float t = tr.cur / tr.dur;
                if (t > 1.0f) t = 1.0f;
                float e = animEaseApply(tr.ease, t);
                double v = tr.from + (tr.to - tr.from) * (double)e;
                animPropSet(o, tr.key.c_str(), tr.comp, v);
                if (t < 1.0f) allDone = false;
            } else {
                // 零时长轨道直接吸附到目标值
                animPropSet(o, tr.key.c_str(), tr.comp, tr.to);
            }
        }
        // 空轨道的状态永远不算“完成”，防止它立刻被停止而无法停留（hold 字段在此未参与判定）
        if (allDone && st.tracks.empty()) allDone = false;
        if (allDone) {
            if (st.loop) {
                for (size_t k = 0; k < st.tracks.size(); ++k) {
                    st.tracks[k].cur = 0.0f;
                    st.tracks[k].started = false;
                }
            } else {
                sm.playing = false;
            }
        }
    }
}

// 返回某网格的默认半尺寸，用于物理包围盒与编辑器选中框。
// 内置网格走硬编码表（这些值与网格的生成参数对应）；自定义网格读网格自己的包围盒，
// 三个轴都至少夹到 0.01，避免退化网格产生零尺寸碰撞体
Vec3 Scene::Gvtcier_Scene_MeshHalfExtents(GvtId<Mesh> mesh) const {
    if (mesh == planeMeshId_) return Vec3(16.0f, 0.1f, 16.0f);
    if (mesh == sphereMeshId_) return Vec3(1.0f, 1.0f, 1.0f);
    if (mesh == coneMeshId_) return Vec3(1.0f, 1.0f, 1.0f);
    if (mesh == planeCircleMeshId_) return Vec3(1.0f, 0.1f, 1.0f);
    // 三角形半高 1、底边半宽 √3/2≈0.866，厚度与其它平面一样取 0.1
    if (mesh == planeTriMeshId_) return Vec3(0.866f, 0.1f, 1.0f);
    if (mesh == cylinderMeshId_) return Vec3(0.5f, 0.5f, 0.5f);
    if (mesh == torusMeshId_) return Vec3(0.85f, 0.25f, 0.85f);
    if (mesh == colliderBoxMeshId_) return Vec3(0.5f, 0.5f, 0.5f);
    if (mesh == colliderCylinderMeshId_) return Vec3(0.5f, 0.5f, 0.5f);
    const Mesh* m = meshStore_.Gvtcier_Object_Get(mesh);
    if (m && m->hasBounds()) {
        Vec3 h = m->localHalf();
        return Vec3(std::fmax(h.x, 0.01f), std::fmax(h.y, 0.01f), std::fmax(h.z, 0.01f));
    }
    return Vec3(0.5f, 0.5f, 0.5f);
}

// 释放场景全部 GPU 资源，注销顺序：先逐个调 destroy 释放显存，再清空登记表。
// 蒙皮网格与骨骼数据一并清理，最后清空对象表使所有旧对象 ID 失效
void Scene::Gvtcier_Scene_Destroy() {
    meshStore_.ForEachAlive([](Mesh& m, GvtId<Mesh>) { m.destroy(); });
    texStore_.ForEachAlive([](Texture& t, GvtId<Texture>) { t.destroy(); });
    meshStore_.Gvtcier_Object_Clear();
    texStore_.Gvtcier_Object_Clear();
    meshes_->pngTexs_.clear();
    meshes_->modelMeshs_.clear();
    skinned_->skinnedMesh_.mesh.destroy();
    skinnedTexId_ = GvtId<Texture>{};
    skinned_->skinnedMesh_.bones.clear();
    skinned_->skinnedMesh_.anims.clear();
    skinned_->skinnedMesh_.ibm.clear();
    skinned_->skinnedMesh_.pose.clear();
    skinnedBoneMats.clear();
    objects_.Gvtcier_Object_Clear();
    nameIndexValid_ = false;
}
