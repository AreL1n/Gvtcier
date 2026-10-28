#include "Gvtcier.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace {

// 物理世界的调参常量集中在此处，方便统一把握手感：

// 固定子步长 1/120 秒。用固定步长而不是可变帧时间，是为了让碰撞求解在低帧率下依旧稳定；
// kMaxSubSteps 限制单帧最多补算 8 步，防止卡顿后一次性追帧造成“爆炸”
constexpr float kSubStep = 1.0f / 120.0f;
constexpr int kMaxSubSteps = 8;
// 角色水平速度低于 0.35 时直接归零，避免输入松开后留下肉眼可见的极小滑行
constexpr float kStopSpeed = 0.35f;
// 所有碰撞体半尺寸的下限 0.06。Jolt 对极薄细节别形状的数值稳定性差，
// 零尺寸形状还会导致接触法线退化，所以统一夹住
constexpr double kMinHalf = 0.06;
// 玩家胶囊的“半总高”：kPlayerCylHalf（圆柱段半高）+ kPlayerRadius（半径），
// 即从胶囊中心到脚底的距离，用于在“脚底位置”和“胶囊中心位置”之间换算
constexpr double kPlayerHalf = kPlayerCylHalf + kPlayerRadius;
// 地面探测射线长度：胶囊半高再加 0.25 的余量，保证站在地面时一定能打到地面
constexpr float kGroundProbeReach = (float)(kPlayerHalf + 0.25);
// 玩家质量 12。取值偏小，让动态物体撞到玩家时能明显推动玩家，手感更轻快
constexpr float kPlayerMass = 12.0f;

// Jolt 的 ObjectLayer 分层：静态世界与可动体。
// 分层的意义在于静态几何之间永远不互相检测，省掉大量无效的窄相碰撞计算
namespace Layers {
    static constexpr JPH::ObjectLayer StaticWorld = 0;
    static constexpr JPH::ObjectLayer Moving = 1;
}

// 宽相位（BroadPhase）层的数量与编号，与 ObjectLayer 一一对应
namespace BPLayers {
    static constexpr JPH::BroadPhaseLayer StaticWorld(0);
    static constexpr JPH::BroadPhaseLayer Moving(1);
    static constexpr JPH::uint NumLayers = 2;
}

// ObjectLayer 到 BroadPhaseLayer 的映射：除 Moving 之外全部落入静态世界层
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NumLayers; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return inLayer == Layers::Moving ? BPLayers::Moving : BPLayers::StaticWorld;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        return inLayer == BPLayers::Moving ? "Moving" : "StaticWorld";
    }
#endif
};

// 物体层与宽相位层的过滤规则：静态世界只与可动体检测，可动体与所有层检测
class ObjectVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        if (inLayer1 == Layers::StaticWorld) return inLayer2 == BPLayers::Moving;
        return true;
    }
};

// 物体层两两之间的过滤规则：静态对静态一律不碰撞，这是静态关卡几何最主要的性能优化
class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
        if (inLayer1 == Layers::StaticWorld && inLayer2 == Layers::StaticWorld) return false;
        return true;
    }
};

// 物理层与场景对象之间的桥接记录。一个场景对象在物理世界里对应一条 Entry，
// 通过 objIdx（GvtId 而非指针）回指场景对象，避免对象销毁后留下悬空指针
struct Entry {
    GvtId<Object> objIdx;
    // Jolt 刚体句柄。刚体本身由 Jolt 的 BodyInterface 管理，
    // 生命周期与其所属 PhysicsImpl 绑定，不能在 Entry 销毁时自行释放
    JPH::BodyID body;
    // 碰撞形状的半尺寸（已按碰撞类型规整，例如球取最大分量作为半径）
    Vec3 half = Vec3(0.5, 0.5, 0.5);
    bool isPlayer = false;
    bool isPlayerBody = false;
    // 是否为有父对象的子物体。子物体的位姿由场景层级驱动，物理层只做跟随
    bool isChild = false;
    // 运动学体：位置由脚本/动画直接设定，但会推开动态物体
    bool kinematic = false;
    // 动态刚体：受重力与碰撞影响，物理层反过来把结果写回场景对象
    bool rigid = false;
    bool hasBody = false;
    // 质量下限在 createBody 里夹到 0.01，避免零质量导致求解矩阵奇异
    float mass = 1.0f;
    Vec3 vel;
    Vec3 angVel;
    // 运动学体的目标速度与绕轴旋转，Step 里据此推演下一帧位姿
    Vec3 kinVel;
    Vec3 kinAngVel;
    Vec3 kinPivot;
    // 上一次同步的位姿，用于判断场景对象是否真的移动（避免每帧重复写刚体）
    Vec3 lastPos;
    Quat lastRot;
    // 形状类别缓存：0 盒 1 球 2 胶囊 3 圆柱 4 网格，调试线框按此选择画法
    int shapeKind = 0;
    // 形状的强引用，保证 Jolt 刚体在存续期间形状不被回收
    JPH::RefConst<JPH::Shape> shape;
};

// 场景传感器（触发器）刚体的包装。同一时刻只允许存在一个传感器，
// 新建时会把旧的销毁
struct SensorBody {
    JPH::BodyID id;
    bool valid = false;
};

// 物理世界的全部实现数据，以 void* impl_ 的形式挂在不暴露 Jolt 头文件的 PhysicsWorld 上
struct PhysicsImpl {
    JPH::PhysicsSystem system;
    JPH::TempAllocatorImpl* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem = nullptr;
    BPLayerInterfaceImpl bpLayer;
    ObjectVsBPLayerFilterImpl objectVsBP;
    ObjectLayerPairFilterImpl layerPair;
    std::vector<Entry> entries;
    // 角色控制器。与普通刚体不同，CharacterVirtual 不是刚体，靠扫掠胶囊自己解算移动；
    // 它同时带一个内部刚体（inner body）以便参与碰撞推送
    JPH::Ref<JPH::CharacterVirtual> character;
    // 玩家在 entries 里的下标，-1 表示尚未创建
    int playerEntry = -1;
    bool grounded = false;
    bool onPlatform = false;
    // 玩家脚底位置（不是胶囊中心）
    Vec3 playerPos;
    Vec3 playerVel;
    // 重力默认 -30，比真实重力大约 3 倍；实际值由 Scene.gravity 取负后覆盖
    Vec3 gravity = Vec3(0, -30.0f, 0);
    PlayerSettings ps;
    const Scene* scene = nullptr;
    bool ready = false;
    struct SensorBody* sensor = nullptr;
};

// Jolt 的全局注册（分配器、类型工厂）是进程级的，不支持重复注册。
// 用引用计数 + 互斥锁管理，允许多个 PhysicsWorld 共存
std::mutex gJoltMutex;
int gJoltUsers = 0;

// 第一个使用者负责完成 Jolt 的全局初始化
void acquireJolt() {
    std::lock_guard<std::mutex> lock(gJoltMutex);
    if (gJoltUsers++ == 0) {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

// 最后一个使用者负责反注册并释放工厂单例
void releaseJolt() {
    std::lock_guard<std::mutex> lock(gJoltMutex);
    if (gJoltUsers > 0 && --gJoltUsers == 0) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

// 下面一组函数是引擎数学类型与 Jolt 类型之间的双向转换。
// Jolt 区分单精度 Vec3 与双精度 RVec3（R 表示 Real，大世界坐标用），
// 位置类数据用 RVec3，方向与速度类数据用 Vec3
JPH::Vec3 toJ(const Vec3& v) {
    return JPH::Vec3((float)v.x, (float)v.y, (float)v.z);
}

JPH::RVec3 toJR(const Vec3& v) {
    return JPH::RVec3(v.x, v.y, v.z);
}

// 转成 Jolt 四元数后强制归一化，防止上层传入带累积误差的旋转角导致求解异常
JPH::Quat toJQ(const Quat& q) {
    return JPH::Quat((float)q.x, (float)q.y, (float)q.z, (float)q.w).Normalized();
}

Vec3 fromJ(JPH::Vec3Arg v) {
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

Vec3 fromJR(JPH::RVec3Arg v) {
    return Vec3(v.GetX(), v.GetY(), v.GetZ());
}

Quat fromJQ(JPH::QuatArg q) {
    return Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()).normalized();
}

void pushTri(std::vector<Vec3>& t, const Vec3& a, const Vec3& b, const Vec3& c) {
    t.push_back(a);
    t.push_back(b);
    t.push_back(c);
}

// 把“非网格”碰撞体离散成三角形列表（局部空间，未叠加位置与旋转），
// 用于 Jolt 的 MeshShape 或调试绘制。半尺寸取缩放的一半，且不小于 kMinHalf。
// 球按 16x8 经纬度细分；柱/胶囊按 16 段环绕，胶囊两端各补 3 圈半球盖；
// 盒体直接按 8 顶点 + 6 面的固定索引表拼出 12 个三角形
void primitiveTris(const Object& o, std::vector<Vec3>& t) {
    Vec3 h(std::fmax(0.5 * std::fabs(o.scale.x), kMinHalf),
           std::fmax(0.5 * std::fabs(o.scale.y), kMinHalf),
           std::fmax(0.5 * std::fabs(o.scale.z), kMinHalf));
    if (o.collider == ColliderType::Sphere) {
        // 球体取三个轴里最大的半长作为半径，保证旋转后仍能包住原盒体；
        // 3.14159265358979 与 6.28318530717959 分别是 π 与 2π
        double r = std::fmax(std::fmax(h.x, h.y), h.z);
        const int seg = 16, rings = 8;
        for (int y = 0; y < rings; ++y) {
            double t0 = 3.14159265358979 * y / rings;
            double t1 = 3.14159265358979 * (y + 1) / rings;
            for (int x = 0; x < seg; ++x) {
                double p0 = 6.28318530717959 * x / seg;
                double p1 = 6.28318530717959 * (x + 1) / seg;
                Vec3 a(r * std::sin(t0) * std::cos(p0), r * std::cos(t0), r * std::sin(t0) * std::sin(p0));
                Vec3 b(r * std::sin(t0) * std::cos(p1), r * std::cos(t0), r * std::sin(t0) * std::sin(p1));
                Vec3 c(r * std::sin(t1) * std::cos(p1), r * std::cos(t1), r * std::sin(t1) * std::sin(p1));
                Vec3 d(r * std::sin(t1) * std::cos(p0), r * std::cos(t1), r * std::sin(t1) * std::sin(p0));
                pushTri(t, a, b, c);
                pushTri(t, a, c, d);
            }
        }
        return;
    }
    if (o.collider == ColliderType::Cylinder || o.collider == ColliderType::Capsule) {
        double r = std::fmax(std::fmax(h.x, h.z), kMinHalf);
        double top = h.y, bot = -h.y, cap = 0.0;
        // 胶囊的圆柱段要各缩短一个半径，剩余高度用半球盖补齐，总高仍为 2*h.y
        if (o.collider == ColliderType::Capsule) {
            top = h.y - r;
            bot = -h.y + r;
            cap = r;
        }
        const int seg = 16;
        for (int x = 0; x < seg; ++x) {
            double p0 = 6.28318530717959 * x / seg;
            double p1 = 6.28318530717959 * (x + 1) / seg;
            double c0 = std::cos(p0), s0 = std::sin(p0), c1 = std::cos(p1), s1 = std::sin(p1);
            // 侧面按两个三角形拼成一个四边形条带
            pushTri(t, Vec3(r * c0, top, r * s0), Vec3(r * c1, top, r * s1), Vec3(r * c1, bot, r * s1));
            pushTri(t, Vec3(r * c0, top, r * s0), Vec3(r * c1, bot, r * s1), Vec3(r * c0, bot, r * s0));
            if (cap > 1e-5) {
                // 胶囊端盖：上下各 3 圈四分之一圆弧，用 sin/cos 参数化出半球面
                for (int y = 0; y < 3; ++y) {
                    double t0 = 1.5707963267949 * y / 3;
                    double t1 = 1.5707963267949 * (y + 1) / 3;
                    double r0 = r * std::cos(t0), r1 = r * std::cos(t1);
                    double y0 = top + cap * std::sin(t0), y1 = top + cap * std::sin(t1);
                    pushTri(t, Vec3(r0 * c0, y0, r0 * s0), Vec3(r0 * c1, y0, r0 * s1), Vec3(r1 * c1, y1, r1 * s1));
                    pushTri(t, Vec3(r0 * c0, y0, r0 * s0), Vec3(r1 * c1, y1, r1 * s1), Vec3(r1 * c0, y1, r0 * s0));
                    double b0 = bot - cap * std::sin(t0), b1 = bot - cap * std::sin(t1);
                    pushTri(t, Vec3(r0 * c0, b0, r0 * s0), Vec3(r0 * c1, b0, r0 * s1), Vec3(r1 * c1, b1, r1 * s1));
                    pushTri(t, Vec3(r0 * c0, b0, r0 * s0), Vec3(r1 * c1, b1, r1 * s1), Vec3(r1 * c0, b1, r0 * s0));
                }
            } else {
                // 纯圆柱：上下各用三角扇封盖
                pushTri(t, Vec3(r * c0, top, r * s0), Vec3(r * c1, top, r * s1), Vec3(0, top, 0));
                pushTri(t, Vec3(r * c0, bot, r * s0), Vec3(0, bot, 0), Vec3(r * c1, bot, r * s1));
            }
        }
        return;
    }
    // 盒体：用 i 的 3 个二进制位决定每个顶点在 x/y/z 上取正还是取负
    Vec3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = Vec3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
    const int faces[6][4] = {{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for (int f = 0; f < 6; ++f) {
        pushTri(t, p[faces[f][0]], p[faces[f][1]], p[faces[f][2]]);
        pushTri(t, p[faces[f][0]], p[faces[f][2]], p[faces[f][3]]);
    }
}

// 按碰撞类型规整出碰撞形状的半尺寸：
// 球取三轴最大值做半径（各轴相同，因此近似为外接球，比原盒体略大）；
// 胶囊半径取水平方向最大值、半高不小于半径，保证圆柱段长度非负；
// 其它类型（盒/柱/网格）逐轴夹到 kMinHalf 下限
Vec3 halfOf(const Object& o) {
    Vec3 h(std::fabs(0.5 * o.scale.x), std::fabs(0.5 * o.scale.y), std::fabs(0.5 * o.scale.z));
    switch (o.collider) {
        case ColliderType::Sphere: {
            double r = std::fmax(std::fmax(h.x, h.y), h.z);
            r = std::fmax(r, kMinHalf);
            return Vec3(r, r, r);
        }
        case ColliderType::Capsule: {
            double r = std::fmax(std::fmax(h.x, h.z), kMinHalf);
            return Vec3(r, std::fmax(h.y, r), r);
        }
        default:
            return Vec3(std::fmax(h.x, kMinHalf), std::fmax(h.y, kMinHalf), std::fmax(h.z, kMinHalf));
    }
}

// 取对象参与碰撞的三角形列表（局部空间）。只有“网格”碰撞类型且网格保留了
// CPU 端顶点索引时才用真实三角形，否则退回按碰撞类型生成的基本几何近似。
// 这正是网格碰撞比盒体碰撞昂贵的地方：三角形数量直接决定窄相检测开销
std::vector<Vec3> meshTris(const Scene* scene, const Object& o) {
    std::vector<Vec3> tris;
    const Mesh* m = scene ? scene->Gvtcier_Scene_GetMesh(o.mesh) : nullptr;
    if (o.collider == ColliderType::Mesh && m && !m->cpuIdx().empty() && !m->cpuVerts().empty()) {
        const std::vector<Vertex>& cv = m->cpuVerts();
        const std::vector<unsigned int>& ci = m->cpuIdx();
        tris.reserve(ci.size());
        for (size_t i = 0; i + 2 < ci.size(); i += 3) {
            for (int k = 0; k < 3; ++k) {
                const Vertex& v = cv[ci[i + k]];
                tris.push_back(Vec3(v.pos.x, v.pos.y, v.pos.z));
            }
        }
        return tris;
    }
    primitiveTris(o, tris);
    return tris;
}

// 根据碰撞类型构造 Jolt 形状：
// - 球用外接球半径；胶囊用 (圆柱段半高, 半径)，半高不足半径时夹到 0；
// - 圆柱用 (半径, 半高)；
// - 网格把三角形列表喂给 MeshShape，若构形失败（空顶点、退化三角形等）
//   退回盒体近似，保证对象仍有碰撞而不是彻底穿模；
// - 其余情况（盒体与未识别的类型）都用盒体
JPH::RefConst<JPH::Shape> shapeOf(const Object& o, const std::vector<Vec3>& tris) {
    Vec3 h = halfOf(o);
    switch (o.collider) {
        case ColliderType::Sphere:
            return new JPH::SphereShape((float)h.x);
        case ColliderType::Capsule:
            return new JPH::CapsuleShape((float)std::fmax(h.y - h.x, 0.0), (float)h.x);
        case ColliderType::Cylinder:
            return new JPH::CylinderShape((float)h.x, (float)h.y);
        case ColliderType::Mesh: {
            if (tris.size() >= 3) {
                JPH::TriangleList list;
                list.reserve(tris.size() / 3);
                for (size_t i = 0; i + 2 < tris.size(); i += 3) {
                    list.push_back(JPH::Triangle(
                        JPH::Float3((float)tris[i].x, (float)tris[i].y, (float)tris[i].z),
                        JPH::Float3((float)tris[i + 1].x, (float)tris[i + 1].y, (float)tris[i + 1].z),
                        JPH::Float3((float)tris[i + 2].x, (float)tris[i + 2].y, (float)tris[i + 2].z)));
                }
                JPH::MeshShapeSettings settings(list);
                JPH::Shape::ShapeResult result;
                JPH::RefConst<JPH::Shape> shape = new JPH::MeshShape(settings, result);
                if (result.IsValid()) return shape;
            }
            return new JPH::BoxShape(JPH::Vec3((float)h.x, (float)h.y, (float)h.z));
        }
        default:
            return new JPH::BoxShape(JPH::Vec3((float)h.x, (float)h.y, (float)h.z));
    }
}

// 动态或运动学体放进 Moving 层，其余放静态层。分层直接决定碰撞过滤行为
JPH::ObjectLayer layerOf(const Entry& e) {
    return (e.rigid || e.kinematic) ? Layers::Moving : Layers::StaticWorld;
}

// 由 Entry 的类型位推断 Jolt 的运动类型。
// 若同时标记 rigid 与 kinematic，rigid 优先；上层构造 Entry 时已保证两者互斥
JPH::EMotionType motionOf(const Entry& e) {
    if (e.rigid) return JPH::EMotionType::Dynamic;
    if (e.kinematic) return JPH::EMotionType::Kinematic;
    return JPH::EMotionType::Static;
}

// 从物理系统移除并销毁刚体，同时释放 Entry 对形状的强引用
void destroyBody(PhysicsImpl* impl, Entry& e) {
    if (!e.hasBody) return;
    JPH::BodyInterface& bi = impl->system.GetBodyInterface();
    bi.RemoveBody(e.body);
    bi.DestroyBody(e.body);
    e.hasBody = false;
    e.shape = nullptr;
}

// 为对象创建刚体并加入物理系统。返回 false 表示形状构造失败或刚体创建失败。
// 关键物理参数的含义：
//   mFriction 取对象自身摩擦系数（0 表示无摩擦，1 左右接近橡胶）；
//   mRestitution 固定 0，即完全非弹性碰撞，落地不弹跳，符合体素/盒体关卡的观感；
//   mLinearDamping 0.05 / mAngularDamping 0.08 是极小的阻尼，只为消除长期数值漂移；
//   mAllowSleeping 允许静止刚体休眠，大幅省 CPU
bool createBody(PhysicsImpl* impl, Entry& e, const Object& o, const std::vector<Vec3>& tris) {
    JPH::RefConst<JPH::Shape> shape = shapeOf(o, tris);
    if (!shape) return false;
    e.shape = shape;
    e.half = halfOf(o);
    // 质量下限 0.01：零或负质量会让 Jolt 的质量属性计算出现除零
    e.mass = (float)std::fmax(o.mass, 0.01);

    {
        // 记录形状子类型编号，供调试线框选择绘制方式
        switch (shape->GetSubType()) {
            case JPH::EShapeSubType::Box: e.shapeKind = 0; break;
            case JPH::EShapeSubType::Sphere: e.shapeKind = 1; break;
            case JPH::EShapeSubType::Capsule: e.shapeKind = 2; break;
            case JPH::EShapeSubType::Cylinder: e.shapeKind = 3; break;
            case JPH::EShapeSubType::Mesh: e.shapeKind = 4; break;
            default: break;
        }
    }

    // 欧拉角（度）先换四元数再交给 Jolt；位置用双精度 RVec3
    JPH::BodyCreationSettings bcs(shape.GetPtr(), toJR(o.pos),
                                  toJQ(Quat::fromEulerDeg(o.rotDeg.x, o.rotDeg.y, o.rotDeg.z)),
                                  motionOf(e), layerOf(e));
    bcs.mAllowSleeping = true;
    bcs.mFriction = (float)o.friction;
    bcs.mRestitution = 0.0f;
    bcs.mLinearDamping = 0.05f;
    bcs.mAngularDamping = 0.08f;
    if (e.rigid) {
        // 只覆盖质量、惯性张量仍按形状计算，这样不同形状的转动惯量才合理
        bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = e.mass;
    }

    JPH::Body* body = impl->system.GetBodyInterface().CreateBody(bcs);
    if (!body) return false;
    e.body = body->GetID();
    e.hasBody = true;
    // 加入时激活，保证新生成的刚体第一帧就参与求解
    impl->system.GetBodyInterface().AddBody(e.body, JPH::EActivation::Activate);
    e.lastPos = o.pos;
    e.lastRot = Quat::fromEulerDeg(o.rotDeg.x, o.rotDeg.y, o.rotDeg.z);
    return true;
}

// 创建玩家角色控制器。与普通刚体的区别：
// - 玩家不是被重力推着滚的刚体，而是“虚拟角色”，每帧由代码给定期望速度后自行扫掠求解；
// - 参数里 mMaxSlopeAngle 50 度是可爬坡上限，超过就被当作墙；
// - mCharacterPadding 0.02 让角色与外物保持一点缝隙，避免贴着表面抖动；
// - mPenetrationRecoverySpeed 1.0 让角色在被挤入物体内部时以每秒 1 单位的速度弹出；
// - mPredictiveContactDistance 0.05 提前预测接触，防止高速穿模；
// - mCollisionTolerance 0.002 是碰撞判定容差，越小越精确但越容易出现间隙抖动；
// - mBackFaceMode 设为与背面也碰撞，避免薄墙从内侧穿过；
// - innerBody 让角色拥有实体，可以推动动态刚体。
// feet 传的是脚底位置，内部加 kPlayerHalf 换算成胶囊中心
bool createPlayerBody(PhysicsImpl* impl, Entry& e, const Vec3& feet) {
    e.isPlayer = true;
    e.half = Vec3(kPlayerRadius, kPlayerHalf, kPlayerRadius);
    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape((float)kPlayerCylHalf, (float)kPlayerRadius);
    e.shape = capsule;

    JPH::CharacterVirtualSettings settings;
    settings.mShape = capsule;
    settings.mMass = kPlayerMass;
    // mMaxStrength 220 是角色能施加的最大推挤力，越大越容易推动重物
    settings.mMaxStrength = 220.0f;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
    settings.mCharacterPadding = 0.02f;
    settings.mPenetrationRecoverySpeed = 1.0f;
    settings.mPredictiveContactDistance = 0.05f;
    settings.mCollisionTolerance = 0.002f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mInnerBodyShape = capsule;
    settings.mInnerBodyLayer = Layers::Moving;

    // 0 是 Jolt 的无效 BodyID 占位参数，最后一个参数是所属物理系统
    impl->character = new JPH::CharacterVirtual(&settings,
                                               toJR(Vec3(feet.x, feet.y + kPlayerHalf, feet.z)),
                                               JPH::Quat::sIdentity(), 0, &impl->system);
    // 角色默认以 Y 轴为“上”，与世界重力的反向一致
    impl->character->SetUp(JPH::Vec3::sAxisY());

    JPH::BodyInterface& bi = impl->system.GetBodyInterface();
    e.body = impl->character->GetInnerBodyID();
    // 内部刚体可能尚未手动加入系统，用 IsAdded 结果初始化 hasBody 以免误移除
    e.hasBody = bi.IsAdded(e.body);
    e.lastPos = Vec3(feet.x, feet.y + kPlayerHalf, feet.z);
    e.lastRot = Quat();
    return true;
}

// 按对象 ID 查 Entry，跳过玩家那条（玩家位姿由角色控制器管理，不走普通过程）
Entry* findEntry(PhysicsImpl* impl, GvtId<Object> id) {
    for (Entry& e : impl->entries) {
        if (e.isPlayer) continue;
        if (e.objIdx == id) return &e;
    }
    return nullptr;
}

// 按刚体 ID 反查 Entry（含玩家），用于射线命中后把 body 还原成场景对象
const Entry* findEntryConst(const PhysicsImpl* impl, JPH::BodyID id) {
    for (const Entry& e : impl->entries) {
        if (e.hasBody && e.body == id) return &e;
    }
    return nullptr;
}

// 过滤掉玩家自己的刚体，避免第一人称射线/探测打到自己
class IgnorePlayerBodyFilter final : public JPH::BodyFilter {
public:
    explicit IgnorePlayerBodyFilter(JPH::BodyID inPlayer) : player(inPlayer) {}
    bool ShouldCollide(const JPH::BodyID& inBodyID) const override { return inBodyID != player; }
private:
    JPH::BodyID player;
};

// 收集“与查询形状接触的刚体 ID”，并跳过 skip 列表里的（通常是传感器自身）
class SensorCollector final : public JPH::CollideShapeBodyCollector {
public:
    explicit SensorCollector(std::vector<JPH::BodyID>& ids,
                             const JPH::BodyID* skip, int skipCount)
        : out(ids), skipIds(skip), nSkip(skipCount) {}
    void AddHit(const JPH::BodyID& inBodyID) override {
        for (int i = 0; i < nSkip; ++i)
            if (skipIds[i] == inBodyID) return;
        out.push_back(inBodyID);
    }
private:
    std::vector<JPH::BodyID>& out;
    const JPH::BodyID* skipIds;
    int nSkip;
};

// 形状重叠查询的收集器：从碰撞结果里取对方刚体 ID（mBodyID2 是“被撞者”），
// 同样跳过 skip 列表。这里可能同一个刚体命中多次（不同子形状），未做去重
class ShapeHitCollector final : public JPH::CollideShapeCollector {
public:
    explicit ShapeHitCollector(std::vector<JPH::BodyID>& ids,
                               const JPH::BodyID* skip, int skipCount)
        : out(ids), skipIds(skip), nSkip(skipCount) {}
    void AddHit(const JPH::CollideShapeResult& inResult) override {
        JPH::BodyID id = inResult.mBodyID2;
        for (int i = 0; i < nSkip; ++i)
            if (skipIds[i] == id) return;
        out.push_back(id);
    }
private:
    std::vector<JPH::BodyID>& out;
    const JPH::BodyID* skipIds;
    int nSkip;
};

// 查询用的宽相位过滤：静态层与可动层都要参与，因此恒返回真
class QueryBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override {
        return inLayer == BPLayers::StaticWorld || inLayer == BPLayers::Moving;
    }
};

class QueryObjectLayerFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer) const override {
        return inLayer == Layers::StaticWorld || inLayer == Layers::Moving;
    }
};

// 可选择性跳过网格形状。网格碰撞代价高，某些查询（例如粗略探测）可以忽略它
class QueryShapeFilter final : public JPH::ShapeFilter {
public:
    explicit QueryShapeFilter(bool inSkipMesh) : skipMesh(inSkipMesh) {}
    bool ShouldCollide(const JPH::Shape* inShape2, const JPH::SubShapeID&) const override {
        if (!inShape2) return false;
        if (skipMesh && inShape2->GetSubType() == JPH::EShapeSubType::Mesh) return false;
        return true;
    }
private:
    bool skipMesh;
};

// 一次射线检测的完整结果。dist 为 -1 表示未命中；
// valid 表示命中的刚体确实能映射回一个存活的场景对象（例如打中玩家时 valid 为假）
struct RayHit {
    GvtId<Object> obj;
    JPH::BodyID body;
    JPH::SubShapeID sub;
    Vec3 point;
    Vec3 normal;
    float dist = -1.0f;
    bool valid = false;
};

using NearestRayCollector = JPH::ClosestHitCollisionCollector<JPH::CastRayCollector>;

// 在物理世界内做一次射线检测，返回最近命中。dir 无需归一化，内部会除以自身长度；
// maxDist 为射线的最大长度。流程：
// 1) 归一化方向，把射线长度乘进方向向量（Jolt 用 mFraction∈[0,1] 表示命中比例）；
// 2) 用 NearestRayCollector 找最近命中，若收集器没拿到结果再用单次 CastRay 兜底；
// 3) 由 fraction 反算命中距离与命中点，再取该点的表面法线；
// 4) 法线统一翻到与射线方向相反的一侧，保证使用方能直接用它做“表面朝向”。
// 副作用：无。命中玩家内部刚体时返回 true 但 outHit.valid 仍为假
bool raycastJolt(PhysicsImpl* impl, const Vec3& origin, const Vec3& dir, float maxDist, bool skipMesh, RayHit& outHit) {
    outHit = RayHit{};
    if (!impl || !impl->ready) return false;
    double dl = dir.length();
    if (dl < 1e-9) return false;
    if (maxDist <= 0.0f) return false;
    Vec3 d = dir / dl;

    JPH::RVec3 rayOrigin = toJR(origin);
    JPH::Vec3 rayDir = toJ(d * (double)maxDist);

    // 射线同样会命中的玩家内部刚体，这里显式过滤
    JPH::BodyID playerBody;
    if (impl->playerEntry >= 0) {
        const Entry& pe = impl->entries[(size_t)impl->playerEntry];
        if (pe.hasBody) playerBody = pe.body;
    }
    IgnorePlayerBodyFilter bodyFilter(playerBody);
    QueryBroadPhaseFilter bpFilter;
    QueryObjectLayerFilter objFilter;

    const JPH::NarrowPhaseQuery& query = impl->system.GetNarrowPhaseQuery();

    JPH::RRayCast ray(rayOrigin, rayDir);
    JPH::RayCastResult best;
    bool found = false;

    {
        // 与背面也碰撞：薄面几何（单面墙、单面地形）从背后打也能命中，
        // mTreatConvexAsSolid 让凸形状内部射出的射线也能得到正确表面
        JPH::RayCastSettings settings;
        settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
        settings.mTreatConvexAsSolid = true;
        NearestRayCollector collector;
        collector.Reset();
        query.CastRay(ray, settings, collector, bpFilter, objFilter, bodyFilter, JPH::ShapeFilter());
        if (collector.HadHit()) {
            best = collector.mHit;
            found = true;
        }
    }

    // 收集器未命中时的兜底：走简化版 CastRay 再试一次
    if (!found) {
        JPH::RayCastResult single;
        if (query.CastRay(ray, single, bpFilter, objFilter, bodyFilter)) {
            best = single;
            found = true;
        }
    }

    if (!found) return false;

    float fraction = best.mFraction;
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    outHit.body = best.mBodyID;
    outHit.sub = best.mSubShapeID2;
    outHit.dist = (float)(maxDist * (double)fraction);
    outHit.point = origin + d * (double)outHit.dist;

    // 读锁访问刚体只为取表面法线。法线长度异常（退化形状）时用反向射线方向兜底，
    // 并与射线同向时取反，保证法线总是“朝向射线来的一侧”
    JPH::BodyLockRead lock(impl->system.GetBodyLockInterface(), best.mBodyID);
    if (lock.Succeeded()) {
        const JPH::Body& body = lock.GetBody();
        Vec3 n = fromJ(body.GetWorldSpaceSurfaceNormal(best.mSubShapeID2, toJR(outHit.point)));
        if (n.length() < 1e-6) n = d * -1.0;
        if (n.dot(d) > 0.0) n = n * -1.0;
        outHit.normal = n;
    } else {
        outHit.normal = d * -1.0;
    }

    // 只有能映射到非玩家场景对象时才置 valid，调用方据此区分“打到物体”与“打到玩家”
    const Entry* e = findEntryConst(impl, best.mBodyID);
    if (e && !e->isPlayer) {
        outHit.obj = e->objIdx;
        outHit.valid = outHit.obj.valid();
    }
    return true;
}

// 从脚底上方 0.1 处向下打一条短射线判断脚下是否有地。
// 起点抬高 0.1 是为了避免射线起点恰好嵌在地面内部导致漏检；
// skipMesh 传 false，即网格地形也参与地面判定
bool probeGround(PhysicsImpl* impl, const Vec3& feet, GvtId<Object>& outObject) {
    RayHit hit;
    if (!raycastJolt(impl, Vec3(feet.x, feet.y + 0.1, feet.z), Vec3(0, -1, 0), kGroundProbeReach, false, hit)) return false;
    if (!hit.valid) return false;
    outObject = hit.obj;
    return true;
}

}

// 初始化物理世界。若已有实例先完整关闭，再重建。
// 步骤：
// 1) 引用计数式初始化 Jolt 全局环境；
// 2) 从场景读重力（Scene.gravity 是“向下加速度大小”，取负得到向量的 y 分量），
//    并扫描场景取第一个玩家对象的出生点作为初始位置；
// 3) 建临时分配器（16 MB 足够一次求解的临时数据）与线程池（1 个工作线程，
//    超过 1 个在小型场景里线程同步开销会抵消并行收益）；
// 4) 初始化 PhysicsSystem，四个数值依次是最大刚体数、最大刚体锁数（0 表示按首参数自动）、
//    最大接触约束数与最大物体对数，数量级按 4096/8192 预留；
// 5) 设置求解参数：位置迭代 2 次、速度迭代 10 次（速度迭代多更稳定），
//    Baumgarte 系数 0.2 控制穿透修正力度，穿透容差 0.02 与投机接触距离 0.02；
// 6) 为场景中所有带碰撞体的非玩家对象建刚体，最后单独建玩家角色；
// 7) 优化宽相位结构，减少后续查询的误判。
// 注意：impl->scene 保存的是场景的地址，场景对象必须比物理世界活得更久
bool PhysicsWorld::Gvtcier_Physics_Init(const Scene& scene, const Player& p, const PlayerSettings& ps) {
    Gvtcier_Physics_Shutdown();
    acquireJolt();

    PhysicsImpl* impl = new PhysicsImpl();
    impl_ = impl;
    impl->scene = &scene;
    impl->ps = ps;
    impl->gravity = Vec3(0, -scene.gravity, 0);
    impl->playerPos = p.pos;

    // 场景里存在的玩家对象优先于传入的 Player 参数，因为它是地图里标注的出生点
    scene.ForEachObject([&](const Object& o, GvtId<Object>) {
        if (o.kind == ObjKind::Player) impl->playerPos = o.pos;
    });

    impl->tempAllocator = new JPH::TempAllocatorImpl(16 * 1024 * 1024);
    impl->jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 1);
    impl->system.Init(4096, 0, 8192, 4096, impl->bpLayer, impl->objectVsBP, impl->layerPair);
    impl->system.SetGravity(toJ(impl->gravity));
    {
        JPH::PhysicsSettings settings = impl->system.GetPhysicsSettings();
        settings.mNumPositionSteps = 2;
        settings.mNumVelocitySteps = 10;
        settings.mBaumgarte = 0.2f;
        settings.mPenetrationSlop = 0.02f;
        settings.mSpeculativeContactDistance = 0.02f;
        impl->system.SetPhysicsSettings(settings);
    }
    impl->ready = true;

    // 遍历场景建刚体。无碰撞体（None）与玩家（单独处理）都跳过。
    // rigid 与 kinematic 的判定是互斥的：kinematic 优先，rigid 需显式非 kinematic
    scene.ForEachObject([&](const Object& o, GvtId<Object> id) {
        if (o.kind == ObjKind::Player) return;
        if (o.collider == ColliderType::None) return;
        Entry e;
        e.objIdx = id;
        e.kinematic = o.physics && o.kinematic;
        e.rigid = o.physics && o.rigidBody && !o.kinematic;
        e.isChild = o.parent.valid();
        e.vel = o.vel;
        e.angVel = o.angVel;
        createBody(impl, e, o, meshTris(&scene, o));
        impl->entries.push_back(e);
    });

    {
        Entry e;
        // 玩家固定放在 entries 末尾，下标存进 playerEntry 以便 O(1) 访问
        impl->playerEntry = (int)impl->entries.size();
        createPlayerBody(impl, e, impl->playerPos);
        impl->entries.push_back(e);
    }

    impl->system.OptimizeBroadPhase();
    return true;
}

// 关闭物理世界。销毁顺序：先释放角色控制器，再删传感器刚体，再逐个删刚体，
// 最后释放线程池、临时分配器和实现对象本身。所有 Jolt 刚体必须在
// PhysicsSystem 还存活时删除，因此不能依赖析构自动处理。
// 结束后 impl_ 置空，重复调用是安全的
void PhysicsWorld::Gvtcier_Physics_Shutdown() {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    impl->character = nullptr;
    if (impl->sensor && impl->sensor->valid && impl->ready) {
        JPH::BodyInterface& bi = impl->system.GetBodyInterface();
        bi.RemoveBody(impl->sensor->id);
        bi.DestroyBody(impl->sensor->id);
        impl->sensor->valid = false;
    }
    delete impl->sensor;
    impl->sensor = nullptr;
    if (impl->ready) {
        JPH::BodyInterface& bi = impl->system.GetBodyInterface();
        for (Entry& e : impl->entries) {
            if (!e.hasBody) continue;
            bi.RemoveBody(e.body);
            bi.DestroyBody(e.body);
            e.hasBody = false;
        }
    }
    delete impl->jobSystem;
    delete impl->tempAllocator;
    delete impl;
    impl_ = nullptr;
    releaseJolt();
}

// 玩家当前是否踩在地面上，由 readPlayerState 每帧根据角色控制器状态更新
bool PhysicsWorld::Gvtcier_Physics_PlayerGrounded() const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    return impl ? impl->grounded : false;
}

// 玩家当前速度。玩家速度不读角色控制器缓存，而是每帧同步进 playerVel
Vec3 PhysicsWorld::Gvtcier_Physics_PlayerVelocity() const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    return impl ? impl->playerVel : Vec3(0, 0, 0);
}

// 不带 ID 的重载，等同传入无效 ID（不会与已有 Entry 匹配，总是新建记录）
bool PhysicsWorld::Gvtcier_Physics_AddObject(const Object& o) {
    return Gvtcier_Physics_AddObject(o, GvtId<Object>{});
}

// 添加或更新一个对象的刚体。已存在同 ID 的 Entry 时先销毁旧刚体再按新属性重建，
// 这样修改碰撞类型/尺寸后不必手动删除；不存在则新增一条记录。
// 玩家对象与无碰撞体对象一律拒绝
bool PhysicsWorld::Gvtcier_Physics_AddObject(const Object& o, GvtId<Object> id) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl || !impl->ready) return false;
    if (o.kind == ObjKind::Player) return false;
    if (o.collider == ColliderType::None) return false;

    if (Entry* e = findEntry(impl, id)) {
        destroyBody(impl, *e);
        e->kinematic = o.physics && o.kinematic;
        e->rigid = o.physics && o.rigidBody && !o.kinematic;
        e->isChild = o.parent.valid();
        return createBody(impl, *e, o, meshTris(impl->scene, o));
    }

    Entry e;
    e.objIdx = id;
    e.kinematic = o.physics && o.kinematic;
    e.rigid = o.physics && o.rigidBody && !o.kinematic;
    e.isChild = o.parent.valid();
    e.vel = o.vel;
    e.angVel = o.angVel;
    bool ok = createBody(impl, e, o, meshTris(impl->scene, o));
    impl->entries.push_back(e);
    return ok;
}

// 移除单个对象的刚体记录。players 记录被跳过，因此不会误删玩家。
// 删除会让 entries 里的下标整体前移，所以必须同步修正 playerEntry，
// 否则玩家下标会指向错误记录
void PhysicsWorld::Gvtcier_Physics_RemoveObject(GvtId<Object> id) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        Entry& e = impl->entries[i];
        if (e.isPlayer || e.objIdx != id) continue;
        destroyBody(impl, e);
        impl->entries.erase(impl->entries.begin() + (long)i);
        if (impl->playerEntry > (int)i) --impl->playerEntry;
        return;
    }
}

// 批量移除。逐个调用 RemoveObject，正好利用了每次移除后修正 playerEntry 的逻辑
void PhysicsWorld::Gvtcier_Physics_RemoveObjects(const std::vector<GvtId<Object>>& ids) {
    for (GvtId<Object> id : ids) Gvtcier_Physics_RemoveObject(id);
}

// 目前是空实现：蒙皮网格不生成物理刚体，骨骼动画只在渲染层生效
void PhysicsWorld::Gvtcier_Physics_RebuildSkinned(const Scene& scene) {
    (void)scene;
}

// 直接把刚体瞬移到指定位置（保留旋转）。对动态刚体会顺带清空线速度与角速度，
// 否则瞬移后的残余速度会让物体立刻飞走；同时同步 Entry 里缓存的上一帧位姿
void PhysicsWorld::Gvtcier_Physics_SetObjectPos(GvtId<Object> id, const Vec3& pos) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    Entry* e = findEntry(impl, id);
    if (!e || !e->hasBody) return;
    JPH::BodyInterface& bi = impl->system.GetBodyInterface();
    bi.SetPositionAndRotation(e->body, toJR(pos), bi.GetRotation(e->body), JPH::EActivation::Activate);
    if (e->rigid) {
        bi.SetLinearVelocity(e->body, JPH::Vec3::sZero());
        bi.SetAngularVelocity(e->body, JPH::Vec3::sZero());
    }
    e->lastPos = pos;
    e->vel = Vec3(0, 0, 0);
    e->angVel = Vec3(0, 0, 0);
}

// 设置动态刚体的线速度。非动态刚体直接忽略，因为运动学/静态体的速度不由求解器决定
void PhysicsWorld::Gvtcier_Physics_SetObjectVelocity(GvtId<Object> id, const Vec3& vel) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    Entry* e = findEntry(impl, id);
    if (!e || !e->hasBody || !e->rigid) return;
    e->vel = vel;
    impl->system.GetBodyInterface().SetLinearVelocity(e->body, toJ(vel));
}

void PhysicsWorld::Gvtcier_Physics_SetObjectAngularVelocity(GvtId<Object> id, const Vec3& angVel) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    Entry* e = findEntry(impl, id);
    if (!e || !e->hasBody || !e->rigid) return;
    e->angVel = angVel;
    impl->system.GetBodyInterface().SetAngularVelocity(e->body, toJ(angVel));
}

// 让刚体跟随场景对象（物理体反向同步）。用 SetPositionAndRotationWhenChanged 而不是
// 每次都写，是因为 Jolt 内部会缓存变换，只有变化时才通知宽相位更新，避免无谓重建。
// DontActivate 保证纯静态跟随不会错误唤醒已休眠的动态物体。
// scale 参数未使用：Jolt 形状在创建时已固化尺寸，运行期改缩放需要重建刚体
void PhysicsWorld::Gvtcier_Physics_FollowObject(GvtId<Object> id, const Vec3& pos, const Vec3& rotDeg, const Vec3& scale) {
    (void)scale;
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    Entry* e = findEntry(impl, id);
    if (!e || !e->hasBody) return;
    Quat q = Quat::fromEulerDeg(rotDeg.x, rotDeg.y, rotDeg.z);
    impl->system.GetBodyInterface().SetPositionAndRotationWhenChanged(e->body, toJR(pos), toJQ(q), JPH::EActivation::DontActivate);
    e->lastPos = pos;
    e->lastRot = q;
}

// 把场景对象的位姿同步到物理刚体（场景驱动物理）。只处理非动态体：
// 动态刚体的位姿应当由求解器决定，反过来写会与重力/碰撞结果打架。
// 根对象且非运动学时先做变化检测：位移小于 1e-5 且四元数各分量变化小于 1e-5 就跳过，
// 避免静止场景每帧都写一遍刚体变换
void PhysicsWorld::Gvtcier_Physics_SyncChildren(const Scene& scene) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    scene.ForEachObject([&](const Object& o, GvtId<Object> id) {
        if (o.kind == ObjKind::Player) return;
        Entry* e = findEntry(impl, id);
        if (!e || !e->hasBody) return;
        if (e->rigid) return;
        if (!o.parent.valid() && !e->kinematic) {
            Vec3 dp(o.pos.x - e->lastPos.x, o.pos.y - e->lastPos.y, o.pos.z - e->lastPos.z);
            Quat nq = Quat::fromEulerDeg(o.rotDeg.x, o.rotDeg.y, o.rotDeg.z);
            bool rotChanged = std::fabs(nq.x - e->lastRot.x) > 1e-5 || std::fabs(nq.y - e->lastRot.y) > 1e-5 ||
                              std::fabs(nq.z - e->lastRot.z) > 1e-5 || std::fabs(nq.w - e->lastRot.w) > 1e-5;
            if (dp.length() < 1e-5 && !rotChanged) return;
        }
        Gvtcier_Physics_FollowObject(id, o.pos, o.rotDeg, o.scale);
    });
}

// 给运动学体设定目标速度、角速度与旋转轴心。三者由 Step 里的推演逻辑消费：
// 线速度直接积分位置；角速度绕 pivot 点的轴旋转，用来做旋转平台、跷跷板之类
void PhysicsWorld::Gvtcier_Physics_SetKinematic(GvtId<Object> id, const Vec3& vel, const Vec3& angVel, const Vec3& pivot) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    Entry* e = findEntry(impl, id);
    if (!e || !e->kinematic) return;
    e->kinVel = vel;
    e->kinAngVel = angVel;
    e->kinPivot = pivot;
}

// 玩家是否站在运动学体（如移动平台）上，由 readPlayerState 根据脚下刚体类型判定
bool PhysicsWorld::Gvtcier_Physics_PlayerOnPlatform() const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    return impl ? impl->onPlatform : false;
}

void PhysicsWorld::Gvtcier_Physics_SetPlayerSpeed(float v) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (impl) impl->ps.speed = v;
}

// 设置走/跑速度。小于 0.01 的值被忽略，防止误传 0 导致角色无法移动
void PhysicsWorld::Gvtcier_Physics_SetWalkRunSpeed(float walk, float run) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    if (walk > 0.01f) impl->ps.walkSpeed = walk;
    if (run > 0.01f) impl->ps.runSpeed = run;
}

// 输出指针允许为空，为空的那一项不写出
void PhysicsWorld::Gvtcier_Physics_GetWalkRunSpeed(float* walk, float* run) const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl) return;
    if (walk) *walk = impl->ps.walkSpeed;
    if (run) *run = impl->ps.runSpeed;
}

void PhysicsWorld::Gvtcier_Physics_SetRunEnabled(bool on) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (impl) impl->ps.runEnabled = on;
}

bool PhysicsWorld::Gvtcier_Physics_RunEnabled() const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    return impl ? impl->ps.runEnabled : false;
}

void PhysicsWorld::Gvtcier_Physics_SetPlayerJump(float v) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (impl) impl->ps.jumpSpeed = v;
}

// 传送玩家到指定脚底位置。角色位置是胶囊中心，所以要加 kPlayerHalf；
// 同时清空速度与地面/平台状态，避免传送后带着旧速度或误判站在原位置的地面上
void PhysicsWorld::Gvtcier_Physics_SetPlayerPos(const Vec3& pos) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl || impl->playerEntry < 0) return;
    Entry& e = impl->entries[(size_t)impl->playerEntry];
    Vec3 center(pos.x, pos.y + kPlayerHalf, pos.z);
    if (impl->character) {
        impl->character->SetPosition(toJR(center));
        impl->character->SetLinearVelocity(JPH::Vec3::sZero());
    }
    e.lastPos = center;
    impl->playerPos = pos;
    impl->playerVel = Vec3(0, 0, 0);
    impl->onPlatform = false;
    impl->grounded = false;
}

// 射线检测并返回命中对象 ID。scene 参数未使用（保留给未来的场景级过滤）。
// 未命中或只打中玩家时返回无效 ID
GvtId<Object> PhysicsWorld::Gvtcier_Physics_CastRay(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDist) {
    (void)scene;
    RayHit hit;
    if (!raycastJolt((PhysicsImpl*)impl_, origin, dir, maxDist, false, hit)) return GvtId<Object>{};
    return hit.obj;
}

// 带详细结果的射线检测。射线打中任何刚体都会写出命中点/法线/距离，
// 但只有打中可映射的场景对象时返回的 ID 才有效；完全未命中时 outDist 写 -1
GvtId<Object> PhysicsWorld::Gvtcier_Physics_CastRayEx(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDist,
                                            Vec3* outPoint, Vec3* outNormal, float* outDist) {
    RayHit hit;
    bool any = raycastJolt((PhysicsImpl*)impl_, origin, dir, maxDist, false, hit);
    if (!any) {
        (void)scene;
        if (outDist) *outDist = -1.0f;
        return GvtId<Object>{};
    }
    if (outPoint) *outPoint = hit.point;
    if (outNormal) *outNormal = hit.normal;
    if (outDist) *outDist = hit.dist;
    return hit.obj;
}

// 只关心“是否打到有效场景表面”的射线查询，返回值即 hit.valid。
// 与 CastRayEx 的区别：打中玩家返回 false，适合地面检测、贴墙判断等场景
bool PhysicsWorld::Gvtcier_Physics_CastRaySurface(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDist,
                                                  Vec3* outPoint, Vec3* outNormal, float* outDist) {
    (void)scene;
    RayHit hit;
    if (!raycastJolt((PhysicsImpl*)impl_, origin, dir, maxDist, false, hit)) {
        if (outDist) *outDist = -1.0f;
        return false;
    }
    if (outPoint) *outPoint = hit.point;
    if (outNormal) *outNormal = hit.normal;
    if (outDist) *outDist = hit.dist;
    return hit.valid;
}

// 创建（或替换）场景传感器。传感器是一个不产生物理响应的静态刚体（mIsSensor），
// 只用于重叠查询。useSphere 决定用球还是盒；半径取三轴绝对值中的最大值，
// 最小 0.01 防止零尺寸形状。同一时刻只保留一个传感器，旧的先销毁
bool PhysicsWorld::Gvtcier_Physics_CreateSensor(const Vec3& center, const Vec3& half, bool useSphere) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl || !impl->ready) return false;
    const float r = (float)std::fmax(std::fmax(std::fabs(half.x), std::fabs(half.y)), std::fabs(half.z));
    JPH::RefConst<JPH::Shape> shape;
    if (useSphere) shape = new JPH::SphereShape(std::fmax(r, 0.01f));
    else shape = new JPH::BoxShape(JPH::Vec3((float)std::fmax(std::fabs(half.x), 0.01),
                                             (float)std::fmax(std::fabs(half.y), 0.01),
                                             (float)std::fmax(std::fabs(half.z), 0.01)));
    // 传感器放在 Moving 层，这样与静态几何、动态体都能配对成功
    JPH::BodyCreationSettings bcs(shape.GetPtr(), toJR(center), JPH::Quat::sIdentity(),
                                  JPH::EMotionType::Static, Layers::Moving);
    bcs.mIsSensor = true;
    JPH::Body* body = impl->system.GetBodyInterface().CreateBody(bcs);
    if (!body) return false;
    impl->system.GetBodyInterface().AddBody(body->GetID(), JPH::EActivation::DontActivate);
    if (!impl->sensor) impl->sensor = new SensorBody();
    if (impl->sensor->valid) {
        JPH::BodyInterface& bi = impl->system.GetBodyInterface();
        bi.RemoveBody(impl->sensor->id);
        bi.DestroyBody(impl->sensor->id);
    }
    impl->sensor->id = body->GetID();
    impl->sensor->valid = true;
    return true;
}

// 销毁传感器并释放包装对象。传感器不存在时安全返回
void PhysicsWorld::Gvtcier_Physics_DestroySensor() {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl || !impl->sensor) return;
    if (impl->sensor->valid) {
        JPH::BodyInterface& bi = impl->system.GetBodyInterface();
        bi.RemoveBody(impl->sensor->id);
        bi.DestroyBody(impl->sensor->id);
        impl->sensor->valid = false;
    }
    delete impl->sensor;
    impl->sensor = nullptr;
}

// 查询当前与传感器重叠的对象 ID 列表，返回命中数量。
// 用传感器的形状在它自己的位置上做一次 CollideShape 查询，跳过传感器自身，
// 最后把刚体 ID 映射回场景对象 ID。
// 注意 half 局部变量算出来后并未参与查询（查询用的是传感器自己的形状），属于冗余计算
int PhysicsWorld::Gvtcier_Physics_QuerySensor(std::vector<GvtId<Object>>& out) const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    out.clear();
    if (!impl || !impl->ready || !impl->sensor || !impl->sensor->valid) return 0;
    JPH::BodyInterface& bi = impl->system.GetBodyInterface();
    JPH::RVec3 pos = bi.GetPosition(impl->sensor->id);
    Vec3 half(0.5, 0.5, 0.5);
    if (const JPH::Shape* s = bi.GetShape(impl->sensor->id)) {
        JPH::AABox box = s->GetLocalBounds();
        half = Vec3(box.mMax.GetX(), box.mMax.GetY(), box.mMax.GetZ());
    }
    JPH::BodyID skip[1] = { impl->sensor->id };
    std::vector<JPH::BodyID> hits;
    ShapeHitCollector collector(hits, skip, 1);
    JPH::CollideShapeSettings settings;
    JPH::RefConst<JPH::Shape> probe = bi.GetShape(impl->sensor->id);
    if (probe) {
        // 缩放取 (1,1,1) 即不缩放；起点与传感器同位置，因此等价于“传感器自身范围与谁重叠”
        impl->system.GetNarrowPhaseQuery().CollideShape(probe.GetPtr(), JPH::Vec3::sReplicate(1.0f),
                                                        JPH::RMat44::sTranslation(pos), settings,
                                                        JPH::RVec3::sZero(), collector);
    }
    for (const JPH::BodyID& id : hits) {
        const Entry* e = findEntryConst(impl, id);
        if (e && !e->isPlayer) out.push_back(e->objIdx);
    }
    return (int)out.size();
}

// 用临时形状做一次重叠查询，不创建刚体，适合脚本里的“这里有没有东西”判断。
// 返回是否有任何命中；out 里给出全部命中的场景对象 ID（同一物体可能重复出现）
bool PhysicsWorld::Gvtcier_Physics_OverlapShape(const Vec3& center, const Vec3& half, bool useSphere,
                                                std::vector<GvtId<Object>>& out) const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    out.clear();
    if (!impl || !impl->ready) return false;
    const float r = (float)std::fmax(std::fmax(std::fabs(half.x), std::fabs(half.y)), std::fabs(half.z));
    JPH::RefConst<JPH::Shape> probe;
    if (useSphere) probe = new JPH::SphereShape(std::fmax(r, 0.01f));
    else probe = new JPH::BoxShape(JPH::Vec3((float)std::fmax(std::fabs(half.x), 0.01),
                                             (float)std::fmax(std::fabs(half.y), 0.01),
                                             (float)std::fmax(std::fabs(half.z), 0.01)));
    std::vector<JPH::BodyID> hits;
    ShapeHitCollector collector(hits, nullptr, 0);
    JPH::CollideShapeSettings settings;
    impl->system.GetNarrowPhaseQuery().CollideShape(probe.GetPtr(), JPH::Vec3::sReplicate(1.0f),
                                                    JPH::RMat44::sTranslation(toJR(center)), settings,
                                                    JPH::RVec3::sZero(), collector);
    for (const JPH::BodyID& id : hits) {
        const Entry* e = findEntryConst(impl, id);
        if (e && !e->isPlayer) out.push_back(e->objIdx);
    }
    return !out.empty();
}

// 生成碰撞体调试线框，输出格式为紧密排列的 float 三元组：
// 每个顶点 7 个 float = 位置 xyz + 颜色 rgb + 透明度 a(恒为 1)，线框按线段对输出。
// 返回实际写入的 float 个数；buffer 不够时提前返回已写数量。
// 颜色约定：绿色为静态体、蓝色为运动学体、灰色为不参与动态求解的普通体。
// 玩家自身不绘制（第一人称看不到自己的胶囊）
int PhysicsWorld::Gvtcier_Physics_CollectWireframe(float* out, int maxFloats) const {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl || !out || maxFloats <= 0) return 0;
    const int kFloatsPerVert = 7;
    int count = 0;
    // 写入一个顶点，空间不足时返回 false 作为中断信号
    auto put = [&](const Vec3& p, float r, float g, float b) -> bool {
        if (count + kFloatsPerVert > maxFloats) return false;
        out[count++] = (float)p.x;
        out[count++] = (float)p.y;
        out[count++] = (float)p.z;
        out[count++] = r;
        out[count++] = g;
        out[count++] = b;
        out[count++] = 1.0f;
        return true;
    };
    // 单位立方体 8 个角（±1）与 12 条棱的索引对
    const int corners[8][3] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
        { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }
    };
    const int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };
    const JPH::BodyInterface& bi = impl->system.GetBodyInterface();

    // 盒体线框：8 个角在局部空间乘以 half，再用旋转矩阵 m 变换（手工展开列主序乘法）
    auto emitBox = [&](const Vec3& c, const Mat4& m, const Vec3& half, float cr, float cg, float cb) -> bool {
        Vec3 w[8];
        for (int i = 0; i < 8; ++i) {
            Vec3 lp(corners[i][0] * half.x, corners[i][1] * half.y, corners[i][2] * half.z);
            w[i] = Vec3(c.x + lp.x * m.m[0] + lp.y * m.m[4] + lp.z * m.m[8],
                        c.y + lp.x * m.m[1] + lp.y * m.m[5] + lp.z * m.m[9],
                        c.z + lp.x * m.m[2] + lp.y * m.m[6] + lp.z * m.m[10]);
        }
        for (int k = 0; k < 12; ++k) {
            if (!put(w[edges[k][0]], cr, cg, cb)) return false;
            if (!put(w[edges[k][1]], cr, cg, cb)) return false;
        }
        return true;
    };

    // 在 XZ 平面（horizontal 为真）或 YZ 平面画一个圆环，y 参数是沿轴方向的偏移。
    // 用 2π 均分 seg 段拼成折线近似圆
    auto emitCircle = [&](const Vec3& c, const Mat4& m, double radius, double y, int seg, bool horizontal, float cr, float cg, float cb) -> bool {
        for (int s = 0; s < seg; ++s) {
            double a0 = 6.28318530717959 * s / seg;
            double a1 = 6.28318530717959 * (s + 1) / seg;
            Vec3 l0, l1;
            if (horizontal) {
                l0 = Vec3(radius * std::cos(a0), y, radius * std::sin(a0));
                l1 = Vec3(radius * std::cos(a1), y, radius * std::sin(a1));
            } else {
                l0 = Vec3(0.0, radius * std::cos(a0) + y, radius * std::sin(a0));
                l1 = Vec3(0.0, radius * std::cos(a1) + y, radius * std::sin(a1));
            }
            Vec3 w0(c.x + l0.x * m.m[0] + l0.y * m.m[4] + l0.z * m.m[8],
                    c.y + l0.x * m.m[1] + l0.y * m.m[5] + l0.z * m.m[9],
                    c.z + l0.x * m.m[2] + l0.y * m.m[6] + l0.z * m.m[10]);
            Vec3 w1(c.x + l1.x * m.m[0] + l1.y * m.m[4] + l1.z * m.m[8],
                    c.y + l1.x * m.m[1] + l1.y * m.m[5] + l1.z * m.m[9],
                    c.z + l1.x * m.m[2] + l1.y * m.m[6] + l1.z * m.m[10]);
            if (!put(w0, cr, cg, cb)) return false;
            if (!put(w1, cr, cg, cb)) return false;
        }
        return true;
    };

    // 球线框：一个水平大圆 + 三个竖直大圆近似。注意下面第三个 emitCircle
    // 与第二个参数完全相同，实际只画出两个竖直圆（第三个被重复绘制）
    auto emitSphere = [&](const Vec3& c, const Mat4& m, double r, float cr, float cg, float cb) -> bool {
        if (!emitCircle(c, m, r, 0.0, 20, true, cr, cg, cb)) return false;
        if (!emitCircle(c, m, r, 0.0, 20, false, cr, cg, cb)) return false;
        if (!emitCircle(c, m, r, 0.0, 20, false, cr, cg, cb)) return false;
        for (int s = 0; s < 4; ++s) {
            double a = 3.14159265358979 * 0.5 * s;
            Vec3 l0(0.0, 0.0, 0.0);
            Vec3 l1(0.0, 0.0, 0.0);
            double ca = std::cos(a), sa = std::sin(a);
            if (s == 0) { l0 = Vec3(r, 0, 0); l1 = Vec3(-r, 0, 0); }
            else if (s == 1) { l0 = Vec3(0, 0, r); l1 = Vec3(0, 0, -r); }
            else { l0 = Vec3(ca * r, sa * r, 0); l1 = Vec3(-ca * r, -sa * r, 0); }
            Vec3 w0(c.x + l0.x * m.m[0] + l0.y * m.m[4] + l0.z * m.m[8],
                    c.y + l0.x * m.m[1] + l0.y * m.m[5] + l0.z * m.m[9],
                    c.z + l0.x * m.m[2] + l0.y * m.m[6] + l0.z * m.m[10]);
            Vec3 w1(c.x + l1.x * m.m[0] + l1.y * m.m[4] + l1.z * m.m[8],
                    c.y + l1.x * m.m[1] + l1.y * m.m[5] + l1.z * m.m[9],
                    c.z + l1.x * m.m[2] + l1.y * m.m[6] + l1.z * m.m[10]);
            if (!put(w0, cr, cg, cb)) return false;
            if (!put(w1, cr, cg, cb)) return false;
        }
        return true;
    };

    // 圆柱线框：上下两个圆 + 每 5 段取一根竖线（20/5=4 根）
    auto emitCylinder = [&](const Vec3& c, const Mat4& m, double r, double halfY, float cr, float cg, float cb) -> bool {
        if (!emitCircle(c, m, r, halfY, 20, true, cr, cg, cb)) return false;
        if (!emitCircle(c, m, r, -halfY, 20, true, cr, cg, cb)) return false;
        for (int s = 0; s < 20; s += 5) {
            double a = 6.28318530717959 * s / 20;
            Vec3 l0(r * std::cos(a), -halfY, r * std::sin(a));
            Vec3 l1(r * std::cos(a), halfY, r * std::sin(a));
            Vec3 w0(c.x + l0.x * m.m[0] + l0.y * m.m[4] + l0.z * m.m[8],
                    c.y + l0.x * m.m[1] + l0.y * m.m[5] + l0.z * m.m[9],
                    c.z + l0.x * m.m[2] + l0.y * m.m[6] + l0.z * m.m[10]);
            Vec3 w1(c.x + l1.x * m.m[0] + l1.y * m.m[4] + l1.z * m.m[8],
                    c.y + l1.x * m.m[1] + l1.y * m.m[5] + l1.z * m.m[9],
                    c.z + l1.x * m.m[2] + l1.y * m.m[6] + l1.z * m.m[10]);
            if (!put(w0, cr, cg, cb)) return false;
            if (!put(w1, cr, cg, cb)) return false;
        }
        return true;
    };

    for (const Entry& e : impl->entries) {
        if (e.isPlayer || !e.hasBody) continue;
        float cr = 0.0f, cg = 1.0f, cb = 0.0f;
        if (e.kinematic) { cr = 0.2f; cg = 0.6f; cb = 1.0f; }
        else if (!e.rigid) { cr = 0.75f; cg = 0.75f; cb = 0.75f; }
        // 从物理系统读实时位姿而不是 Entry 缓存，保证线框与实际碰撞位置一致
        Vec3 c = fromJR(bi.GetPosition(e.body));
        Mat4 m = fromJQ(bi.GetRotation(e.body)).toMat4();
        bool ok = true;
        switch (e.shapeKind) {
            case 1:
                ok = emitSphere(c, m, e.half.x, cr, cg, cb);
                break;
            case 2: {
                // 胶囊：中段画圆柱（半高缩短一个半径），两端各画一个球，组合出胶囊轮廓。
                // 注意这里球心用世界坐标直接加 y 偏移，且旋转矩阵传的是单位阵，
                // 因此旋转后的胶囊线框不会跟随倾斜，只适用于轴对齐情况
                double r = e.half.x;
                double cyl = std::fmax(e.half.y - r, 0.0);
                ok = emitCylinder(c, m, r, cyl, cr, cg, cb);
                if (ok) {
                    Vec3 top(c.x, c.y + cyl, c.z);
                    Vec3 bot(c.x, c.y - cyl, c.z);
                    ok = emitSphere(top, Mat4(), r, cr, cg, cb) && emitSphere(bot, Mat4(), r, cr, cg, cb);
                }
                break;
            }
            case 3:
                ok = emitCylinder(c, m, e.half.x, e.half.y, cr, cg, cb);
                break;
            default:
                // 盒体与网格（shapeKind 0/4）统一按包围盒绘制
                ok = emitBox(c, m, e.half, cr, cg, cb);
                break;
        }
        if (!ok) return count;
    }
    return count;
}

namespace {

// 把玩家输入转换成角色控制器的线速度。这是玩家手感的核心：
// 1) 水平速度用“向目标速度插值”的方式逼近，而不是直接赋值，这样加速/减速自然；
//    blend = min(1, accel*dt) 保证一帧最多完全追平，乘 airControl 让空中操控力更弱；
// 2) 松开方向键且在地面时，按 decel 做指数衰减，低于 kStopSpeed 直接归零；
// 3) 跳跃只在“本帧请求且在地面”时生效并消耗请求；离地时清掉请求，
//    避免落地瞬间意外弹起（不做土狼时间与跳跃缓冲）；
// 4) 竖直方向自己积分重力，且踩地时把向下速度压 0；
//    -80 是终端速度限制，防止高空坠落时单帧位移过大穿模。
// 副作用：写角色控制器速度；会清掉 p.jumpRequested
void applyPlayerInput(PhysicsImpl* impl, Player& p, float dt) {
    if (impl->playerEntry < 0) return;
    if (!impl->character) return;

    JPH::Vec3 cur = impl->character->GetLinearVelocity();
    double vx = cur.GetX();
    double vz = cur.GetZ();
    double vy = cur.GetY();

    // 输入方向长度和大于 0.01 视为有移动意图（阈值用于过滤摇杆死区的小噪声）
    bool moving = std::fabs(p.wishDir.x) + std::fabs(p.wishDir.z) > 0.01;
    // ps.speed 大于 0 时它是强制速度（例如剧情限速），否则按 跑/走 切换
    double target = impl->ps.speed > 0.01
                    ? impl->ps.speed
                    : ((impl->ps.runEnabled && p.running) ? impl->ps.runSpeed : impl->ps.walkSpeed);

    double wantX = p.wishDir.x * target;
    double wantZ = p.wishDir.z * target;
    double blend = std::fmin(1.0, impl->ps.accel * dt);
    if (!impl->grounded) blend *= impl->ps.airControl;

    vx += (wantX - vx) * blend;
    vz += (wantZ - vz) * blend;

    if (!moving && impl->grounded) {
        double dk = std::fmin(1.0, impl->ps.decel * dt);
        vx *= (1.0 - dk);
        vz *= (1.0 - dk);
        if (vx * vx + vz * vz < kStopSpeed * kStopSpeed) {
            vx = 0.0;
            vz = 0.0;
        }
    }

    if (p.jumpRequested && impl->grounded) {
        vy = impl->ps.jumpSpeed;
        p.jumpRequested = false;
    }
    if (!impl->grounded) p.jumpRequested = false;

    vy += impl->gravity.y * dt;
    if (impl->grounded && vy < 0.0) vy = 0.0;
    if (vy < -80.0) vy = -80.0;

    impl->character->SetLinearVelocity(JPH::Vec3((float)vx, (float)vy, (float)vz));
}

// 把角色控制器的结果写回 Player 与内部状态：
// 角色位置是胶囊中心，减去 kPlayerHalf 得到脚底位置（Scene 与脚本统一用脚底坐标）；
// 地面状态直接取 Jolt 的 EGroundState::OnGround；若脚下刚体是运动学体则标记 onPlatform，
// 供上层决定是否把平台速度叠加到玩家上
void readPlayerState(PhysicsImpl* impl, Player& p) {
    if (impl->playerEntry < 0) return;
    if (!impl->character) return;
    JPH::RVec3 pos = impl->character->GetPosition();
    impl->playerPos = Vec3(pos.GetX(), pos.GetY() - kPlayerHalf, pos.GetZ());
    p.pos = impl->playerPos;
    impl->playerVel = fromJ(impl->character->GetLinearVelocity());
    impl->grounded = impl->character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    impl->onPlatform = false;
    if (impl->grounded) {
        JPH::BodyID groundBody = impl->character->GetGroundBodyID();
        const Entry* ge = findEntryConst(impl, groundBody);
        if (ge && ge->kinematic) impl->onPlatform = true;
    }
}

// 调用角色的 ExtendedUpdate（带贴地/上下台阶的扩展扫掠）。
// 贴地下探 0.15 让角色在走下缓坡时紧贴地面、不会一格一格地弹跳；
// 上台阶高度 0.28 大约是普通台阶的高度，能自动迈上小台阶；
// 最小前伸 0.04 与前方试探距离 0.10 决定台阶检测的灵敏度。
// 后四个空参数依次是角色与其他物体接触/碰撞的回调，这里不关心
void updateCharacter(PhysicsImpl* impl, float dt) {
    if (!impl->character) return;
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.15f, 0.0f);
    settings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.28f, 0.0f);
    settings.mWalkStairsMinStepForward = 0.04f;
    settings.mWalkStairsStepForwardTest = 0.10f;
    impl->character->ExtendedUpdate(dt,
                                    toJ(impl->gravity),
                                    settings,
                                    {},
                                    {},
                                    {},
                                    {},
                                    *impl->tempAllocator);
}

}

// 物理帧的主循环。执行顺序不可随意调换：
// 1) 先按目标速度推演所有运动学体（此时它们还没参与求解，先把位姿摆到新位置）；
// 2) 写玩家速度；
// 3) PhysicsSystem.Update 做宽相/窄相/约束求解，得到动态刚体的新位姿；
// 4) 角色控制器 ExtendedUpdate 单独扫掠移动玩家（它不在刚体求解器里）；
// 5) 读回玩家状态；
// 6) 把动态/运动学刚体的结果写回场景对象，并标脏以便下一帧刷新世界矩阵。
// 子对象（有父）不回写，因为它们的位姿由场景层级决定。
// 场景对象可能已在别处被销毁，Gvtcier_Scene_GetObject 会因世代号失效返回空，
// 此时整条 Entry 被跳过（Entry 本身不会被清理，仍留在 entries 里直到显式移除）。
// 说明：函数当前没有使用 kSubStep/kMaxSubSteps，实际是直接把整帧 dt 交给 Jolt 与角色控制器，
// 固定子步长的常量在此未被消费
void PhysicsWorld::Gvtcier_Physics_Step(Player& p, Scene& scene, float dt) {
    PhysicsImpl* impl = (PhysicsImpl*)impl_;
    if (!impl || !impl->ready) return;

    JPH::BodyInterface& bi = impl->system.GetBodyInterface();

    // 运动学体推演：无角速度时直接按 线速度*dt 平移；
    // 有角速度时围绕 pivot（相对物体本地坐标的偏移）旋转：先把 pivot 转到世界，
    // 用增量四元数旋转后再把 pivot 反向偏移补回，保证旋转不改变轴心点位置
    for (Entry& e : impl->entries) {
        if (e.isPlayer || !e.hasBody || !e.kinematic) continue;
        Vec3 np = e.lastPos + e.kinVel * dt;
        Quat nr = e.lastRot;
        double w = e.kinAngVel.length();
        if (w > 1e-7) {
            Quat dq = Quat::fromAxisAngle(e.kinAngVel / w, w * dt);
            Vec3 hinge = e.lastPos + e.lastRot * e.kinPivot;
            nr = (dq * e.lastRot).normalized();
            np = hinge - nr * e.kinPivot;
        }
        e.lastPos = np;
        e.lastRot = nr;
        bi.SetPositionAndRotation(e.body, toJR(np), toJQ(nr), JPH::EActivation::Activate);
    }

    applyPlayerInput(impl, p, dt);
    impl->system.Update(dt, 1, impl->tempAllocator, impl->jobSystem);
    updateCharacter(impl, dt);

    readPlayerState(impl, p);

    for (Entry& e : impl->entries) {
        if (e.isPlayer || !e.hasBody) continue;
        if (!e.rigid && !e.kinematic) continue;
        Object* o = scene.Gvtcier_Scene_GetObject(e.objIdx);
        if (!o) continue;
        if (o->parent.valid()) continue;
        o->pos = fromJR(bi.GetPosition(e.body));
        o->rotDeg = fromJQ(bi.GetRotation(e.body)).toEulerDeg();
        o->Gvtcier_Object_MarkXformDirty();
        // 运动学体不回写速度：它的速度是输入量，写回场景只会覆盖脚本设定的值
        if (e.kinematic) {
            o->kinVel = e.kinVel;
            o->kinAngVel = e.kinAngVel;
            continue;
        }
        e.vel = fromJ(bi.GetLinearVelocity(e.body));
        e.angVel = fromJ(bi.GetAngularVelocity(e.body));
        o->vel = e.vel;
        o->angVel = e.angVel;
    }
}
