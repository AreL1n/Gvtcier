#pragma once
#include <vector>
#include <deque>
#include <string>
#include "bicu/core/math.h"
#include "bicu/core/export.h"
#include "bicu/core/identity.h"
#include "Gvtcier3D/render/fx.h"

struct Object;

enum class CurveType { Float, Vec2, Vec3, Color };
enum class InterpMode { Linear, Bezier, Step };

struct Keyframe {
    float time = 0.0f;
    float v0 = 0.0f, v1 = 0.0f, v2 = 0.0f, v3 = 1.0f;
    InterpMode interp = InterpMode::Linear;
};

struct CurveEvaluator {
    CurveType type = CurveType::Float;
    std::vector<Keyframe> keys;
    GVTCIER_API float Gvtcier_Effect_EvalFloat(float t) const;
    GVTCIER_API Vec2 Gvtcier_Effect_EvalVec2(float t) const;
    GVTCIER_API Vec3 Gvtcier_Effect_EvalVec3(float t) const;
    GVTCIER_API FxColor Gvtcier_Effect_EvalColor(float t) const;
    GVTCIER_API void Gvtcier_Effect_SetDefault(float v);
    GVTCIER_API void Gvtcier_Effect_SetDefault(const Vec3& v);
    GVTCIER_API void Gvtcier_Effect_SetDefault(const FxColor& v);
};

enum class EmitterShape { Point, Sphere, SphereSurface, Box, Cone, Hemisphere, Ring };

struct ParticleTemplate {
    float lifeMin = 0.5f, lifeMax = 1.5f;
    float sizeMin = 0.1f, sizeMax = 0.3f;
    float speedMin = 1.0f, speedMax = 3.0f;
    float gravity = 0.0f;
    float growth = 0.0f;
    bool alphaBlend = false;
    bool castShadow = false;
    CurveEvaluator colorCurve;
    CurveEvaluator sizeCurve;

    float spread = 45.0f;
    float flatness = 0.0f;

    float linearAccelMin = 0.0f, linearAccelMax = 0.0f;
    float radialAccelMin = 0.0f, radialAccelMax = 0.0f;
    float tangentialAccelMin = 0.0f, tangentialAccelMax = 0.0f;
    float dampingMin = 0.0f, dampingMax = 0.0f;
    float orbitVelMin = 0.0f, orbitVelMax = 0.0f;
    float angularVelMin = 0.0f, angularVelMax = 0.0f;
    bool dampingAsFriction = false;

    bool turbulenceEnabled = false;
    float turbulenceStrength = 1.0f;
    float turbulenceScale = 9.0f;
    float turbulenceSpeed = 1.0f;
    float turbulenceSpeedRandom = 0.2f;
    float turbulenceInfluenceMin = 0.1f, turbulenceInfluenceMax = 0.1f;
    float turbulenceInitDispMin = 0.0f, turbulenceInitDispMax = 0.0f;

    bool alignYToVelocity = false;
    bool rotateY = false;

    int renderKind = 0;
};

struct Emitter {
    std::string name = "Emitter";
    int maxParticles = 256;
    float emissionRate = 20.0f;
    float duration = -1.0f;
    bool looping = true;
    bool warmup = false;
    bool nodeVelocity = true;
    bool nodeGravity = true;
    bool nodeColor = true;
    bool nodeSize = true;
    ParticleTemplate particle;
    EmitterShape shape = EmitterShape::Point;
    Vec3 shapeSize = Vec3(0.5f, 0.5f, 0.5f);
    Vec3 shapeDir = Vec3(0, 1, 0);
    float shapeConeAngle = 30.0f;
    Vec3 gravityDir = Vec3(0, -1, 0);
    float gravityStrength = 0.0f;
    std::string onInitScript;
    std::string onUpdateScript;
    std::string onParticleScript;
    std::string onFinishScript;
};

struct EffectAsset {
    int version = 2;
    std::vector<Emitter> emitters;
    float maxDuration = -1.0f;
    std::string scriptPath;
    GVTCIER_API bool Gvtcier_Effect_LoadJSON(const char* path);
};

struct EffectParticle {
    Vec3 pos;
    Vec3 vel;
    float life = 1.0f, maxLife = 1.0f;
    float size = 0.2f;
    float baseSize = 0.2f;
    float growth = 0.0f;
    FxColor color = { 1, 1, 1, 1 };
    float gravity = 0.0f;
    bool alphaBlend = false;

    Vec3 origin;
    float linearAccel = 0.0f;
    float radialAccel = 0.0f;
    float tangentialAccel = 0.0f;
    float damping = 0.0f;
    float orbitVel = 0.0f;
    float angle = 0.0f;
    float angularVel = 0.0f;
    bool dampingAsFriction = false;

    bool turbEnabled = false;
    float turbStrength = 1.0f;
    float turbScale = 9.0f;
    float turbSpeed = 1.0f;
    float turbSpeedRandom = 0.2f;
    float turbInfluence = 0.0f;
    float seed = 0.0f;

    bool alignYToVelocity = false;
    bool rotateY = false;
    int renderKind = 0;
};

struct EmitterRuntime {
    int emitterIndex = 0;
    float emitAccum = 0.0f;
    float age = 0.0f;
    bool finished = false;
    std::vector<EffectParticle> particles;
};

class EffectInstance {
public:
    GVTCIER_API void Gvtcier_Effect_Update(float dt, FxSystem& fx);
    GVTCIER_API bool Gvtcier_Effect_IsAlive() const;
    GVTCIER_API void Gvtcier_Effect_Stop();
    GVTCIER_API void Gvtcier_Effect_Reset(GvtId<EffectAsset> assetId, const EffectAsset& a, bool lp = true);
    std::string name;
    GvtId<EffectAsset> asset;
    Gvtcier_Object<EffectAsset>* assetStore = nullptr;
    Vec3 position;
    GvtId<Object> boundObject;
    std::string boundAssetPath;
    uint64_t id = 0;
    float age = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = false;
    std::vector<EmitterRuntime> emitters;
};

class EffectSystem {
public:
    GVTCIER_API void Gvtcier_Effect_SetAssetStore(Gvtcier_Object<EffectAsset>* store);
    GVTCIER_API void Gvtcier_Effect_Update(float dt, FxSystem& fx);
    GVTCIER_API void Gvtcier_Effect_Clear();
    GVTCIER_API GvtId<EffectInstance> Gvtcier_Effect_Play(GvtId<EffectAsset> assetId, const Vec3& pos,
                                     bool loop = true, GvtId<Object> bindObj = GvtId<Object>{});
    GVTCIER_API void Gvtcier_Effect_Stop(const std::string& name);
    GVTCIER_API void Gvtcier_Effect_StopAll();
    GVTCIER_API bool Gvtcier_Effect_IsPlaying(const std::string& name) const;
    GVTCIER_API GvtId<EffectInstance> Gvtcier_Effect_FindByObject(GvtId<Object> obj);
    GVTCIER_API GvtId<EffectInstance> Gvtcier_Effect_FindById(uint64_t id);
    GVTCIER_API bool Gvtcier_Effect_StopById(uint64_t id);
    GVTCIER_API bool Gvtcier_Effect_SetPositionById(uint64_t id, const Vec3& pos);
    const Gvtcier_Object<EffectInstance>& instances() const { return instances_; }
    Gvtcier_Object<EffectInstance>& instances() { return instances_; }
    static const int kMaxInstances = 128;
private:
    Gvtcier_Object<EffectInstance> instances_;
    Gvtcier_Object<EffectAsset>* assetStore_ = nullptr;
    uint64_t nextId_ = 0;
};
