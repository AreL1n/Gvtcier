#include "Gvtcier.h"
#include <cstddef>
#include <cmath>
#include <map>
#include <vector>

using namespace rhi;

// 把 CPU 侧的 Vertex 逐个转成 GPU 顶点布局 GpuVertex。
// 两者字段顺序一致，区别只在类型：位置、法线、uv、平滑法线在 Vertex 里是 double，
// 到 GPU 端必须压成 float 才能塞进顶点缓冲；骨骼索引和权重本来就是字节/浮点，原样拷贝。
// vcount 是顶点个数，out 必须已经能容纳 vcount 个元素。
void packGpuVertices(const Vertex* verts, int vcount, GpuVertex* out) {
    for (int i = 0; i < vcount; ++i) {
        const Vertex& v = verts[i];
        GpuVertex& g = out[i];
        g.pos[0] = (float)v.pos.x; g.pos[1] = (float)v.pos.y; g.pos[2] = (float)v.pos.z;
        g.normal[0] = (float)v.normal.x; g.normal[1] = (float)v.normal.y; g.normal[2] = (float)v.normal.z;
        g.uv[0] = (float)v.uv.x; g.uv[1] = (float)v.uv.y;
        for (int k = 0; k < 4; ++k) { g.joint[k] = v.joint[k]; g.weight[k] = v.weight[k]; }
        g.smoothNormal[0] = (float)v.smoothNormal.x;
        g.smoothNormal[1] = (float)v.smoothNormal.y;
        g.smoothNormal[2] = (float)v.smoothNormal.z;
    }
}

// 为一套顶点生成“平滑法线”，写到每个顶点的 smoothNormal 字段。
// 用于卡通描边等需要连续法线的场合：直接用法线外扩会在硬边处裂开，得先把
// 位置重合、朝向接近的顶点法线平均成同一根，描边轮廓才连得上。
//
// 算法分三步：
//   1. 空间分桶——把顶点位置按 kEps(1e-4) 量化成整数格键，位置几乎重合的顶点落进同一个桶
//   2. 桶内聚类——同一个位置上的顶点按法线夹角再分簇，与簇平均法线点积 >= kCosThreshold(0.5)
//      也就是夹角在 60 度以内的算“同一侧”，归到一起；否则另开一簇。
//      这样立方体的棱角两侧法线会分成两簇，不会被错误地磨圆
//   3. 回填——每个顶点取自己所属簇的平均法线并归一化，写进 smoothNormal
// 退化情况：原始法线长度为 0 的顶点跳过聚类，smoothNormal 兜底成 (0,1,0)。
// 直接原地修改 verts，不改变顶点数量与顺序。
void buildSmoothNormals(std::vector<Vertex>& verts) {
    const float kEps = 1e-4f;
    const float kCosThreshold = 0.5f;
    struct Key {
        long long x, y, z;
        bool operator<(const Key& o) const {
            return x != o.x ? x < o.x : (y != o.y ? y < o.y : z < o.z);
        }
    };
    struct Cluster { Vec3 sum; };
    std::map<Key, std::vector<Cluster> > groups;
    std::vector<int> vertGroup(verts.size(), -1);
    for (size_t i = 0; i < verts.size(); ++i) {
        const Vec3& p = verts[i].pos;
        Vec3 n = verts[i].normal;
        float len = n.length();
        if (len <= 1e-9f) continue;
        n = n * (1.0f / len);
        Key k;
        k.x = (long long)std::floor(p.x / kEps + 0.5f);
        k.y = (long long)std::floor(p.y / kEps + 0.5f);
        k.z = (long long)std::floor(p.z / kEps + 0.5f);
        std::vector<Cluster>& list = groups[k];
        int chosen = -1;
        for (size_t g = 0; g < list.size(); ++g) {
            Vec3 avg = list[g].sum;
            float alen = avg.length();
            if (alen <= 1e-9f) continue;
            if (avg.dot(n) / alen >= kCosThreshold) { chosen = (int)g; break; }
        }
        if (chosen < 0) {
            Cluster c;
            c.sum = n;
            list.push_back(c);
            chosen = (int)list.size() - 1;
        }
        else {
            list[chosen].sum = list[chosen].sum + n;
        }
        vertGroup[i] = chosen;
    }
    for (size_t i = 0; i < verts.size(); ++i) {
        if (vertGroup[i] < 0) { verts[i].smoothNormal = Vec3(0, 1, 0); continue; }
        const Vec3& p = verts[i].pos;
        Key k;
        k.x = (long long)std::floor(p.x / kEps + 0.5f);
        k.y = (long long)std::floor(p.y / kEps + 0.5f);
        k.z = (long long)std::floor(p.z / kEps + 0.5f);
        std::map<Key, std::vector<Cluster> >::iterator it = groups.find(k);
        Vec3 n = (it != groups.end()) ? it->second[vertGroup[i]].sum : verts[i].normal;
        float len = n.length();
        verts[i].smoothNormal = len > 1e-9f ? n * (1.0f / len) : Vec3(0, 1, 0);
    }
}

// 用一份 CPU 顶点/索引数据创建 GPU 网格，同时保留一份 CPU 副本。
// 保留 CPU 副本是为了后面做包围盒、重算法线等操作不必回读显存。
// 流程：
//   1. 先 destroy 掉旧资源，重复调用安全
//   2. 若顶点里没填 smoothNormal（长度为 0），认为调用方没提供，自动算一遍
//   3. 逐顶点统计局部 AABB（localMin_/localMax_），供视锥剔除与 BVH 使用
//   4. 建 VAO/VBO/EBO 并绑定属性槽：
//        0 pos、1 normal、2 uv、3 joint、4 weight、5 smoothNormal
//      joint 用 4 个 UnsignedByte（骨骼索引最多 255 根），其余是 float
// 参数：vcount 顶点数，icount 索引数（为 0 表示不用索引，但本实现的绘制路径都按索引走）。
// 返回 false 表示参数非法（顶点为空、索引非空却没给指针）。
bool Mesh::create(const Vertex* verts, int vcount, const unsigned int* idx, int icount) {
    destroy();
    if (vcount <= 0 || !verts || (icount > 0 && !idx)) return false;
    std::vector<Vertex> fixed(verts, verts + vcount);
    bool needSmooth = false;
    for (size_t i = 0; i < fixed.size() && !needSmooth; ++i)
        if (fixed[i].smoothNormal.length() < 1e-9f) needSmooth = true;
    if (needSmooth) buildSmoothNormals(fixed);
    cpuVerts_ = fixed;
    cpuIdx_.assign(idx, idx + icount);
    localMin_ = fixed[0].pos;
    localMax_ = fixed[0].pos;
    for (size_t i = 1; i < fixed.size(); ++i) {
        const Vec3& p = fixed[i].pos;
        localMin_.x = std::fmin(localMin_.x, p.x); localMax_.x = std::fmax(localMax_.x, p.x);
        localMin_.y = std::fmin(localMin_.y, p.y); localMax_.y = std::fmax(localMax_.y, p.y);
        localMin_.z = std::fmin(localMin_.z, p.z); localMax_.z = std::fmax(localMax_.z, p.z);
    }
    hasBounds_ = true;
    rhi::Gvtcier_Rhi_GenVertexArrays(1, &vao_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &vbo_);
    rhi::Gvtcier_Rhi_GenBuffers(1, &ebo_);

    rhi::Gvtcier_Rhi_BindVertexArray(vao_);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, vbo_);
    std::vector<GpuVertex> gpu(vcount);
    packGpuVertices(fixed.data(), vcount, gpu.data());
    rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)(vcount * sizeof(GpuVertex)), gpu.data(), StaticDraw);
    rhi::Gvtcier_Rhi_BindBuffer(ElementArrayBuffer, ebo_);
    rhi::Gvtcier_Rhi_BufferData(ElementArrayBuffer, (size_t)(icount * sizeof(unsigned int)), idx, StaticDraw);

    rhi::Gvtcier_Rhi_VertexAttribPointer(0, 3, Float, false, sizeof(GpuVertex), (const void*)0);
    rhi::Gvtcier_Rhi_EnableAttrib(0);
    rhi::Gvtcier_Rhi_VertexAttribPointer(1, 3, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, normal));
    rhi::Gvtcier_Rhi_EnableAttrib(1);
    rhi::Gvtcier_Rhi_VertexAttribPointer(2, 2, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, uv));
    rhi::Gvtcier_Rhi_EnableAttrib(2);
    rhi::Gvtcier_Rhi_VertexAttribPointer(3, 4, UnsignedByte, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, joint));
    rhi::Gvtcier_Rhi_EnableAttrib(3);
    rhi::Gvtcier_Rhi_VertexAttribPointer(4, 4, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, weight));
    rhi::Gvtcier_Rhi_EnableAttrib(4);
    rhi::Gvtcier_Rhi_VertexAttribPointer(5, 3, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, smoothNormal));
    rhi::Gvtcier_Rhi_EnableAttrib(5);

    rhi::Gvtcier_Rhi_BindVertexArray(0);
    indexCount_ = icount;
    return true;
}

// 释放网格持有的全部 GPU 资源（含实例化用的 VAO/VBO）并清空 CPU 副本。
// CPU 副本也一起清，因为它的顶点数与显存里的已经对不上了，留着只会误导。
void Mesh::destroy() {
    if (vao_) { rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { rhi::Gvtcier_Rhi_DeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { rhi::Gvtcier_Rhi_DeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (instVao_) { rhi::Gvtcier_Rhi_DeleteVertexArrays(1, &instVao_); instVao_ = 0; }
    if (instVbo_) { rhi::Gvtcier_Rhi_DeleteBuffers(1, &instVbo_); instVbo_ = 0; }
    instCapacity_ = 0;
    indexCount_ = 0;
    cpuVerts_.clear();
    cpuIdx_.clear();
    hasBounds_ = false;
}

// 以索引方式画一遍这个网格（非实例化路径）。
// 前提：调用方已经 Use 好着色器并上传了 uniform，这里只负责绑 VAO 与 draw call，
// 不会改光栅状态，也不会恢复绑定。
void Mesh::draw() const {
    rhi::Gvtcier_Rhi_BindVertexArray(vao_);
    rhi::Gvtcier_Rhi_DrawElements(Triangles, indexCount_, UnsignedInt, nullptr);
}

// 确保实例化用的顶点缓冲至少能放下 count 个实例，不够就按 2 的幂扩容。
// 实例数据布局是每个实例 24 个 float：
//   [0..15] 一个 4x4 矩阵，占属性槽 6~9（每个槽 4 个 float，divisor=1）
//   [16..19] 颜色/色调 tint，占槽 10
//   [20..23] uvScale、材质 id 等杂项参数，占槽 11
// 第一次调用时才建 instVao 和 instVbo，并把基础的 0~5 号属性重新绑到新 VAO 上，
// 因为 VAO 里的属性绑定不能从别的 VAO 继承。
// 扩容时只做 BufferData(nullptr) 重新分配容量，不保留旧数据（实例数据每帧都会整批重传）。
// 已存在且容量够时直接返回，不产生任何 GPU 调用。
void Mesh::ensureInstanceCapacity(int count) {
    if (indexCount_ <= 0 || vbo_ == 0 || ebo_ == 0) return;
    if (count <= instCapacity_ && instVao_ != 0) return;
    if (count <= 0) count = 1;
    int cap = instCapacity_ > 0 ? instCapacity_ : 16;
    while (cap < count) cap *= 2;
    if (instVao_ == 0) {
        rhi::Gvtcier_Rhi_GenVertexArrays(1, &instVao_);
        rhi::Gvtcier_Rhi_GenBuffers(1, &instVbo_);
        rhi::Gvtcier_Rhi_BindVertexArray(instVao_);
        rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, vbo_);
        rhi::Gvtcier_Rhi_VertexAttribPointer(0, 3, Float, false, sizeof(GpuVertex), (const void*)0);
        rhi::Gvtcier_Rhi_EnableAttrib(0);
        rhi::Gvtcier_Rhi_VertexAttribPointer(1, 3, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, normal));
        rhi::Gvtcier_Rhi_EnableAttrib(1);
        rhi::Gvtcier_Rhi_VertexAttribPointer(2, 2, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, uv));
        rhi::Gvtcier_Rhi_EnableAttrib(2);
        rhi::Gvtcier_Rhi_VertexAttribPointer(3, 4, UnsignedByte, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, joint));
        rhi::Gvtcier_Rhi_EnableAttrib(3);
        rhi::Gvtcier_Rhi_VertexAttribPointer(4, 4, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, weight));
        rhi::Gvtcier_Rhi_EnableAttrib(4);
        rhi::Gvtcier_Rhi_VertexAttribPointer(5, 3, Float, false, sizeof(GpuVertex), (const void*)offsetof(GpuVertex, smoothNormal));
        rhi::Gvtcier_Rhi_EnableAttrib(5);
        rhi::Gvtcier_Rhi_BindBuffer(ElementArrayBuffer, ebo_);
        rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, instVbo_);
        const int stride = (int)(sizeof(float) * 24);
        for (unsigned int i = 0; i < 4; ++i) {
            unsigned int loc = 6 + i;
            rhi::Gvtcier_Rhi_VertexAttribPointer(loc, 4, Float, false, stride, (const void*)(size_t)(i * sizeof(float) * 4));
            rhi::Gvtcier_Rhi_EnableAttrib(loc);
            rhi::Gvtcier_Rhi_VertexAttribDivisor(loc, 1);
        }
        rhi::Gvtcier_Rhi_VertexAttribPointer(10, 4, Float, false, stride, (const void*)(size_t)(sizeof(float) * 16));
        rhi::Gvtcier_Rhi_EnableAttrib(10);
        rhi::Gvtcier_Rhi_VertexAttribDivisor(10, 1);
        rhi::Gvtcier_Rhi_VertexAttribPointer(11, 4, Float, false, stride, (const void*)(size_t)(sizeof(float) * 20));
        rhi::Gvtcier_Rhi_EnableAttrib(11);
        rhi::Gvtcier_Rhi_VertexAttribDivisor(11, 1);
        rhi::Gvtcier_Rhi_BindVertexArray(0);
    }
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, instVbo_);
    rhi::Gvtcier_Rhi_BufferData(ArrayBuffer, (size_t)cap * 24 * sizeof(float), nullptr, DynamicDraw);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, 0);
    instCapacity_ = cap;
}

// 把一批实例数据整块传到实例缓冲的开头。
// data 必须是 instanceCount*24 个 float，布局与 ensureInstanceCapacity 里描述的一致。
// 调用前必须已经通过 ensureInstanceCapacity 保证容量足够，这里不会自动扩容。
void Mesh::uploadInstanceData(const float* data, int instanceCount) {
    if (instanceCount <= 0 || instVbo_ == 0) return;
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, instVbo_);
    rhi::Gvtcier_Rhi_BufferSubData(ArrayBuffer, 0, (size_t)instanceCount * 24 * sizeof(float), data);
    rhi::Gvtcier_Rhi_BindBuffer(ArrayBuffer, 0);
}

// 一次 draw call 画 instanceCount 个实例。
// 前置条件：调用方已经把实例数据传到 instVbo 并 Use 了支持实例化的着色器；
// 没准备好时会静默跳过，不会崩溃。
void Mesh::drawInstanced(int instanceCount) const {
    if (instanceCount <= 0 || instVao_ == 0 || indexCount_ <= 0) return;
    rhi::Gvtcier_Rhi_BindVertexArray(instVao_);
    rhi::Gvtcier_Rhi_DrawElementsInstanced(Triangles, indexCount_, UnsignedInt, nullptr, instanceCount);
}

// 只绑实例化 VAO，不画东西。给需要自己控制 draw 方式（比如带 scissor 的批量）的调用方留的口子。
void Mesh::bindInstanceVao() const {
    rhi::Gvtcier_Rhi_BindVertexArray(instVao_);
}

// 生成一个以原点为中心、边长为 size 的立方体。
// F 是 6 个面 × 4 个顶点的数据，每个顶点 8 个分量：位置 xyz、法线 xyz、uv uv。
// 顶点不共享——每个面各存 4 个顶点，这样棱角处才能有各自的硬法线（法线不做平均）。
// 每个面用同一套 IDX 组成两个三角形（0-1-2、0-2-3），面外侧按 CCW 绕序，与引擎
// 默认的 frontFace=Ccw、剔除背面一致。
Mesh makeCube(float size) {
    const float h = size * 0.5f;
    static const float F[6][4][8] = {
        {{ h,-h, h, 1,0,0, 0,0},{ h,-h,-h, 1,0,0, 1,0},{ h, h,-h, 1,0,0, 1,1},{ h, h, h, 1,0,0, 0,1}},
        {{-h,-h,-h,-1,0,0, 0,0},{-h,-h, h,-1,0,0, 1,0},{-h, h, h,-1,0,0, 1,1},{-h, h,-h,-1,0,0, 0,1}},
        {{-h, h, h, 0,1,0, 0,1},{ h, h, h, 0,1,0, 1,1},{ h, h,-h, 0,1,0, 1,0},{-h, h,-h, 0,1,0, 0,0}},
        {{-h,-h,-h, 0,-1,0, 0,0},{ h,-h,-h, 0,-1,0, 1,0},{ h,-h, h, 0,-1,0, 1,1},{-h,-h, h, 0,-1,0, 0,1}},
        {{-h,-h, h, 0,0,1, 0,0},{ h,-h, h, 0,0,1, 1,0},{ h, h, h, 0,0,1, 1,1},{-h, h, h, 0,0,1, 0,1}},
        {{ h,-h,-h, 0,0,-1, 0,0},{-h,-h,-h, 0,0,-1, 1,0},{-h, h,-h, 0,0,-1, 1,1},{ h, h,-h, 0,0,-1, 0,1}},
    };
    static const unsigned int IDX[6][6] = {
        {0,1,2, 0,2,3}, {0,1,2, 0,2,3}, {0,1,2, 0,2,3},
        {0,1,2, 0,2,3}, {0,1,2, 0,2,3}, {0,1,2, 0,2,3},
    };

    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    verts.reserve(24);
    idx.reserve(36);
    for (int f = 0; f < 6; ++f) {
        unsigned int base = (unsigned int)verts.size();
        for (int c = 0; c < 4; ++c) {
            Vertex v;
            v.pos = Vec3(F[f][c][0], F[f][c][1], F[f][c][2]);
            v.normal = Vec3(F[f][c][3], F[f][c][4], F[f][c][5]);
            v.uv = Vec2(F[f][c][6], F[f][c][7]);
            verts.push_back(v);
        }
        for (int i = 0; i < 6; ++i) idx.push_back(base + IDX[f][i]);
    }

    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个位于 XZ 平面、朝上（法线 +Y）的地面网格。
// w 是 X 方向宽度，d 是 Z 方向深度，segs 是每个方向的细分数，
// uvRepeat 控制 uv 的平铺倍数（比如贴瓷砖时传 8，一张贴图就重复 8 次）。
// 细分数越多越平滑，代价是顶点数按 (segs+1)^2 增长。
Mesh makePlane(float w, float d, int segs, float uvRepeat) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    const int cols = segs + 1;
    verts.reserve(cols * cols);
    idx.reserve(segs * segs * 6);

    for (int j = 0; j <= segs; ++j)
        for (int i = 0; i <= segs; ++i) {
            Vertex v;
            v.pos = Vec3(-w * 0.5f + w * (float)i / segs, 0.0f, -d * 0.5f + d * (float)j / segs);
            v.normal = Vec3(0, 1, 0);
            v.uv = Vec2(uvRepeat * (float)i / segs, uvRepeat * (float)j / segs);
            verts.push_back(v);
        }

    for (int j = 0; j < segs; ++j)
        for (int i = 0; i < segs; ++i) {
            int a = j * cols + i;
            int b = a + 1;
            int c = a + cols;
            int d = c + 1;
            idx.push_back((unsigned int)a); idx.push_back((unsigned int)c); idx.push_back((unsigned int)b);
            idx.push_back((unsigned int)b); idx.push_back((unsigned int)c); idx.push_back((unsigned int)d);
        }

    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个圆锥（侧面 + 底面圆盘），半径 radius、圆周分段数 slices。
// 高度固定取直径 2*radius，底面放在 y=-radius、顶点放在 y=+radius，所以整体以原点为中心。
// 侧面每个三角形单独生成三个顶点，法线按圆锥的参数化公式求：
//   法线 ∝ (高度*x, 半径^2, 高度*z)，这样侧面是光滑着色的而不是平面着色。
// 底面是一个三角扇，法线统一朝下 (0,-1,0)，顶点与侧面不共享，保证底边的硬边效果。
Mesh makeCone(float radius, int slices) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    verts.reserve((size_t)slices * 3 + (size_t)slices + 2);
    idx.reserve((size_t)slices * 6);
    const float twoPi = 6.2831853f;
    const float height = 2.0f * radius;
    for (int i = 0; i < slices; ++i) {
        float a0 = twoPi * (float)i / slices;
        float a1 = twoPi * (float)(i + 1) / slices;
        float x0 = cosf(a0) * radius, z0 = sinf(a0) * radius;
        float x1 = cosf(a1) * radius, z1 = sinf(a1) * radius;
        float am = (a0 + a1) * 0.5f;
        float xm = cosf(am) * radius, zm = sinf(am) * radius;
        Vertex apex, l, r;
        apex.pos = Vec3(0, radius, 0);
        apex.normal = Vec3(height * xm, radius * radius, height * zm).normalized();
        apex.uv = Vec2(((float)i + 0.5f) / slices, 1.0f);
        l.pos = Vec3(x0, -radius, z0);
        l.normal = Vec3(height * x0, radius * radius, height * z0).normalized();
        l.uv = Vec2((float)i / slices, 0.0f);
        r.pos = Vec3(x1, -radius, z1);
        r.normal = Vec3(height * x1, radius * radius, height * z1).normalized();
        r.uv = Vec2((float)(i + 1) / slices, 0.0f);
        unsigned base = (unsigned)verts.size();
        verts.push_back(apex);
        verts.push_back(l);
        verts.push_back(r);
        idx.push_back(base); idx.push_back(base + 2); idx.push_back(base + 1);
    }
    Vertex baseC;
    baseC.pos = Vec3(0, -radius, 0); baseC.normal = Vec3(0, -1, 0); baseC.uv = Vec2(0.5f, 0.5f);
    verts.push_back(baseC);
    int baseCenter = (int)verts.size() - 1;
    int rimStart = (int)verts.size();
    for (int i = 0; i <= slices; ++i) {
        float a = twoPi * (float)i / slices;
        float x = cosf(a) * radius, z = sinf(a) * radius;
        Vertex v;
        v.pos = Vec3(x, -radius, z);
        v.normal = Vec3(0, -1, 0);
        v.uv = Vec2(0.5f + x * 0.5f / radius, 0.5f + z * 0.5f / radius);
        verts.push_back(v);
    }
    for (int i = 0; i < slices; ++i) {
        idx.push_back((unsigned)baseCenter);
        idx.push_back((unsigned)(rimStart + i));
        idx.push_back((unsigned)(rimStart + i + 1));
    }
    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个 UV 球：radius 半径，stacks 是南北方向分段（纬度），slices 是东西方向分段（经度）。
// 顶点用球坐标参数化：phi 从 0（北极，cos=1）到 180 度（南极），theta 绕 Y 轴一圈。
// 法线直接等于归一化后的位置，所以是标准的逐顶点光滑球面。
// 注意两极处仍然保留了一整圈重复顶点（标准经纬网格做法），UV 接缝也在 theta=0/360 处重复。
Mesh makeSphere(float radius, int stacks, int slices) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    verts.reserve((stacks + 1) * (slices + 1));
    idx.reserve(stacks * slices * 6);

    for (int j = 0; j <= stacks; ++j)
        for (int i = 0; i <= slices; ++i) {
            float phi = deg2rad(180.0f * (float)j / stacks);
            float theta = deg2rad(360.0f * (float)i / slices);
            Vec3 p(
                radius * std::sin(phi) * std::cos(theta),
                radius * std::cos(phi),
                radius * std::sin(phi) * std::sin(theta));
            Vertex v;
            v.pos = p;
            v.normal = p.normalized();
            v.uv = Vec2((float)i / slices, (float)j / stacks);
            verts.push_back(v);
        }

    for (int j = 0; j < stacks; ++j)
        for (int i = 0; i < slices; ++i) {
            int a = j * (slices + 1) + i;
            int b = a + 1;
            int c = a + (slices + 1);
            int d = c + 1;
            idx.push_back((unsigned int)a); idx.push_back((unsigned int)b); idx.push_back((unsigned int)c);
            idx.push_back((unsigned int)b); idx.push_back((unsigned int)d); idx.push_back((unsigned int)c);
        }

    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个位于 XY 平面、朝向 +Z 的单面矩形（宽 w、高 h，以原点为中心）。
// 主要给“编号/文字/图标牌”这类需要正对相机的物体当底板用。
Mesh makePlaneFacing(float w, float h) {
    std::vector<Vertex> verts(4);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    verts[0].pos = Vec3(-hw, -hh, 0.0f); verts[0].normal = Vec3(0, 0, 1); verts[0].uv = Vec2(0.0f, 0.0f);
    verts[1].pos = Vec3(hw, -hh, 0.0f);  verts[1].normal = Vec3(0, 0, 1); verts[1].uv = Vec2(1.0f, 0.0f);
    verts[2].pos = Vec3(hw, hh, 0.0f);   verts[2].normal = Vec3(0, 0, 1); verts[2].uv = Vec2(1.0f, 1.0f);
    verts[3].pos = Vec3(-hw, hh, 0.0f);  verts[3].normal = Vec3(0, 0, 1); verts[3].uv = Vec2(0.0f, 1.0f);
    unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };
    Mesh m;
    m.create(verts.data(), 4, idx, 6);
    return m;
}

// 生成一个位于 XZ 平面、朝上（法线 +Y）的圆盘，半径 radius、圆周分段数 slices。
// 结构是三角扇：中心一个顶点，外圈 slices+1 个顶点（首尾重合以保证 UV 连续）。
// 注意绕序是 0→i+1→i，与 makePlane 相反，是为了让面朝 +Y 时保持 CCW 正面。
Mesh makePlaneCircle(float radius, int slices) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    const float twoPi = 6.2831853f;
    Vertex center;
    center.pos = Vec3(0, 0, 0); center.normal = Vec3(0, 1, 0); center.uv = Vec2(0.5f, 0.5f);
    verts.push_back(center);
    for (int i = 0; i <= slices; ++i) {
        float a = twoPi * (float)i / slices;
        float x = cosf(a) * radius, z = sinf(a) * radius;
        Vertex v;
        v.pos = Vec3(x, 0, z);
        v.normal = Vec3(0, 1, 0);
        v.uv = Vec2(0.5f + x * 0.5f, 0.5f + z * 0.5f);
        verts.push_back(v);
    }
    for (int i = 0; i < slices; ++i) {
        idx.push_back(0);
        idx.push_back((unsigned)(1 + i + 1));
        idx.push_back((unsigned)(1 + i));
    }
    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个位于 XZ 平面、朝上（法线 +Y）的等边三角形，外接圆半径 radius。
// 0.866 就是 sin(60°)，用它算出另外两个顶点在 ±60 度方向上的 X 坐标，
// 三个顶点分别放在角度 90°、210°、330° 上，这样三角形尖头朝 +Z。
// 常用于角色脚底的方向指示片。
Mesh makePlaneTriangle(float radius) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    const float h = radius;
    Vertex v0;
    v0.pos = Vec3(0, 0, h); v0.normal = Vec3(0, 1, 0); v0.uv = Vec2(0.5f, 1.0f);
    verts.push_back(v0);
    Vertex v1;
    v1.pos = Vec3(-h * 0.866f, 0, -h * 0.5f); v1.normal = Vec3(0, 1, 0); v1.uv = Vec2(0.0f, 0.0f);
    verts.push_back(v1);
    Vertex v2;
    v2.pos = Vec3(h * 0.866f, 0, -h * 0.5f); v2.normal = Vec3(0, 1, 0); v2.uv = Vec2(1.0f, 0.0f);
    verts.push_back(v2);
    idx.push_back(0); idx.push_back(1); idx.push_back(2);
    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个以原点为中心、Y 轴为轴向的圆柱：radius 半径，height 高，slices 圆周分段。
// 三部分拼成：侧面筒身（每个分段一对上下顶点，法线水平朝外）、
// 底面圆盘（中心点，法线 0,-1,0）、顶面圆盘（中心点，法线 0,1,0）。
// 三部分顶点不共享，保证上下边缘是硬边而不是被平均成斜面。
Mesh makeCylinder(float radius, float height, int slices) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    const float hy = height * 0.5f;
    const float twoPi = 6.2831853f;
    int sideBase = (int)verts.size();
    for (int i = 0; i <= slices; ++i) {
        float a = twoPi * (float)i / slices;
        float x = cosf(a) * radius, z = sinf(a) * radius;
        Vertex v;
        v.pos = Vec3(x, -hy, z);
        v.normal = Vec3(x / radius, 0.0f, z / radius);
        v.uv = Vec2((float)i / slices, 0.0f);
        verts.push_back(v);
        v.pos = Vec3(x, hy, z);
        v.uv = Vec2((float)i / slices, 1.0f);
        verts.push_back(v);
    }
    for (int i = 0; i < slices; ++i) {
        int a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        idx.push_back((unsigned)a); idx.push_back((unsigned)b); idx.push_back((unsigned)c);
        idx.push_back((unsigned)b); idx.push_back((unsigned)d); idx.push_back((unsigned)c);
    }
    int botC = (int)verts.size();
    {
        Vertex v;
        v.pos = Vec3(0, -hy, 0); v.normal = Vec3(0, -1, 0); v.uv = Vec2(0.5f, 0.5f);
        verts.push_back(v);
    }
    int topC = (int)verts.size();
    {
        Vertex v;
        v.pos = Vec3(0, hy, 0); v.normal = Vec3(0, 1, 0); v.uv = Vec2(0.5f, 0.5f);
        verts.push_back(v);
    }
    for (int i = 0; i < slices; ++i) {
        int b0 = sideBase + i * 2, b1 = sideBase + (i + 1) * 2;
        idx.push_back((unsigned)botC); idx.push_back((unsigned)b0); idx.push_back((unsigned)b1);
        idx.push_back((unsigned)topC); idx.push_back((unsigned)(b1 + 1)); idx.push_back((unsigned)(b0 + 1));
    }
    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}

// 生成一个绕 Y 轴的圆环（甜甜圈）：major 是主半径（圆环中心到管心的距离），
// minor 是管半径（截面圆的半径），seg 是沿主圆方向分段，ring 是沿截面圆方向分段。
// 做法：先沿主圆取一点 tc 和该点的切线方向 tx，再在“切线方向 + 世界 Y”构成的截面平面上
// 绕一圈，得到管面顶点。法线就是 (顶点 - 管心) 归一化，天然是光滑管面。
Mesh makeTorus(float major, float minor, int seg, int ring) {
    std::vector<Vertex> verts;
    std::vector<unsigned int> idx;
    const float twoPi = 6.2831853f;
    for (int j = 0; j <= ring; ++j) {
        float b = twoPi * (float)j / ring;
        Vec3 tc(cosf(b) * major, 0.0f, sinf(b) * major);
        Vec3 tx(-sinf(b), 0.0f, cosf(b));
        for (int i = 0; i <= seg; ++i) {
            float a = twoPi * (float)i / seg;
            Vec3 p = tc + tx * (cosf(a) * minor) + Vec3(0, 1, 0) * (sinf(a) * minor);
            Vertex v;
            v.pos = p;
            v.normal = (p - tc).normalized();
            v.uv = Vec2((float)i / seg, (float)j / ring);
            verts.push_back(v);
        }
    }
    for (int j = 0; j < ring; ++j)
        for (int i = 0; i < seg; ++i) {
            int a = j * (seg + 1) + i;
            int b = a + 1;
            int c = a + (seg + 1);
            int d = c + 1;
            idx.push_back((unsigned)a); idx.push_back((unsigned)b); idx.push_back((unsigned)c);
            idx.push_back((unsigned)b); idx.push_back((unsigned)d); idx.push_back((unsigned)c);
        }
    Mesh m;
    m.create(verts.data(), (int)verts.size(), idx.data(), (int)idx.size());
    return m;
}
