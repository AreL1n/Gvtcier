#pragma once
#include <string>
#include <vector>
#include "Rhidriver/rhi.h"
#include "bicu/core/math.h"

struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv;
    unsigned char joint[4] = { 0, 0, 0, 0 };
    float weight[4] = { 0, 0, 0, 0 };
    Vec3 smoothNormal;
};

struct GpuVertex {
    float pos[3];
    float normal[3];
    float uv[2];
    unsigned char joint[4];
    float weight[4];
    float smoothNormal[3];
};

GVTCIER_API void packGpuVertices(const Vertex* verts, int vcount, GpuVertex* out);

GVTCIER_API void buildSmoothNormals(std::vector<Vertex>& verts);

class Mesh {
public:
    Mesh() = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& o) noexcept { moveFrom(o); }
    Mesh& operator=(Mesh&& o) noexcept {
        if (this != &o) { destroy(); moveFrom(o); }
        return *this;
    }
    GVTCIER_API bool create(const Vertex* verts, int vcount, const unsigned int* idx, int icount);
    GVTCIER_API void destroy();
    GVTCIER_API void draw() const;
    GVTCIER_API int indexCount() const { return indexCount_; }
    const std::vector<Vertex>& cpuVerts() const { return cpuVerts_; }
    const std::vector<unsigned int>& cpuIdx() const { return cpuIdx_; }

    GVTCIER_API void ensureInstanceCapacity(int count);
    GVTCIER_API void uploadInstanceData(const float* data, int instanceCount);
    GVTCIER_API void drawInstanced(int instanceCount) const;
    GVTCIER_API unsigned int instanceVao() const { return instVao_; }
    GVTCIER_API void bindInstanceVao() const;

    GVTCIER_API const Vec3& localMin() const { return localMin_; }
    GVTCIER_API const Vec3& localMax() const { return localMax_; }
    GVTCIER_API Vec3 localCenter() const { return (localMin_ + localMax_) * 0.5f; }
    GVTCIER_API Vec3 localHalf() const { return (localMax_ - localMin_) * 0.5f; }
    GVTCIER_API bool hasBounds() const { return hasBounds_; }

private:
    void moveFrom(Mesh& o) {
        vao_ = o.vao_; vbo_ = o.vbo_; ebo_ = o.ebo_;
        instVao_ = o.instVao_; instVbo_ = o.instVbo_; instCapacity_ = o.instCapacity_;
        indexCount_ = o.indexCount_;
        cpuVerts_ = std::move(o.cpuVerts_);
        cpuIdx_ = std::move(o.cpuIdx_);
        localMin_ = o.localMin_; localMax_ = o.localMax_; hasBounds_ = o.hasBounds_;
        o.vao_ = 0; o.vbo_ = 0; o.ebo_ = 0; o.indexCount_ = 0;
        o.instVao_ = 0; o.instVbo_ = 0; o.instCapacity_ = 0;
        o.cpuVerts_.clear();
        o.cpuIdx_.clear();
        o.hasBounds_ = false;
    }
    unsigned int vao_ = 0, vbo_ = 0, ebo_ = 0;
    unsigned int instVao_ = 0, instVbo_ = 0;
    int instCapacity_ = 0;
    int indexCount_ = 0;
    std::vector<Vertex> cpuVerts_;
    std::vector<unsigned int> cpuIdx_;
    Vec3 localMin_;
    Vec3 localMax_;
    bool hasBounds_ = false;
};

GVTCIER_API Mesh makeCube(float size);
GVTCIER_API Mesh makePlane(float w, float d, int segs, float uvRepeat);
GVTCIER_API Mesh makePlaneFacing(float w, float h);
GVTCIER_API Mesh makeCone(float radius, int slices);
GVTCIER_API Mesh makeSphere(float radius, int stacks, int slices);
GVTCIER_API Mesh makePlaneCircle(float radius, int slices);
GVTCIER_API Mesh makePlaneTriangle(float radius);
GVTCIER_API Mesh makeCylinder(float radius, float height, int slices);
GVTCIER_API Mesh makeTorus(float major, float minor, int seg, int ring);

struct SkinnedBone {
    int parent = -1;
    Vec3 pos;
    Vec4 rotQ = Vec4(0, 0, 0, 1);
    Vec3 dir = Vec3(0, 1, 0);
    float halfLen = 0.1f;
    float radius = 0.09f;
    std::string name;
    int node = -1;
    Vec3 bindT;
    Vec4 bindR = Vec4(0, 0, 0, 1);
    Vec3 bindS = Vec3(1, 1, 1);
};
struct SkinnedBonePose {
    Vec3 t;
    Vec4 r = Vec4(0, 0, 0, 1);
    Vec3 s = Vec3(1, 1, 1);
};
struct SkinnedAnimChannel {
    int bone = -1;
    int path = 0;
    int count = 0;
    int comps = 0;
    float step = 0.0f;
    std::vector<float> times;
    std::vector<float> values;
};
struct SkinnedAnim {
    std::string name;
    float duration = 0.0f;
    std::vector<SkinnedAnimChannel> channels;
};
struct SkinnedMesh {
    Mesh mesh;
    std::vector<SkinnedBone> bones;
    std::vector<float> ibm;
    std::vector<SkinnedAnim> anims;
    std::vector<SkinnedBonePose> pose;
    Mat4 rootTransform = Mat4::identity();
    std::vector<unsigned char> baseColorPixels;
    int baseColorW = 0;
    int baseColorH = 0;
    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool hasBaseColorTex = false;
};
