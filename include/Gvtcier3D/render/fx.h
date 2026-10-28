#pragma once
#include <vector>
#include "bicu/core/math.h"
#include "bicu/core/export.h"

class EffectSystem;

struct FxColor {
    float r, g, b, a;
};

// 粒子朝向模式。默认完全面向相机；烟柱、火焰这类竖直特效要用 LOOKAT_Y，
// 否则相机俯仰时纸片会翻转；拖尾、流星用 VELOCITY 让长边顺速度方向。
enum FxFacing {
    FxLookAtXyz = 0,
    FxLookAtY = 1,
    FxVelocity = 2,
};

struct FxParticle {
    Vec3 pos, vel;
    float life = 1.0f, maxLife = 1.0f;
    float size = 0.2f;
    float growth = 0.0f;
    FxColor color = { 1, 1, 1, 1 };
    float gravity = 0.0f;
    bool alphaBlend = false;
    int kind = 0;
    int facing = FxLookAtXyz;
    float rotation = 0.0f;
    float spin = 0.0f;
};

struct FxBeam {
    Vec3 a, b;
    float width = 0.1f;
    FxColor color = { 1, 0.2f, 0.2f, 0.9f };
};

struct FxLight {
    Vec3 pos;
    FxColor color;
    float radius = 5.0f;
    bool castShadow = false;
    float intensity = 1.0f;
    Vec3 dir;
    float spotCos = -1.0f;
};

class FxSystem {
public:
    GVTCIER_API void Gvtcier_Fx_Update(float dt);
    GVTCIER_API void Gvtcier_Fx_Clear();

    GVTCIER_API void Gvtcier_Fx_Emit(const FxParticle& p);

    GVTCIER_API void Gvtcier_Fx_AddBeam(const Vec3& a, const Vec3& b, const FxColor& color, float width);
    GVTCIER_API void Gvtcier_Fx_AddLight(const Vec3& pos, const FxColor& color, float radius, bool castShadow = false, float intensity = 1.0f);
    GVTCIER_API void Gvtcier_Fx_AddPersistentLight(const Vec3& pos, const FxColor& color, float radius, bool castShadow = false, float intensity = 1.0f);

    EffectSystem* effectSystem() { return effectSystem_; }
    void setEffectSystem(EffectSystem* s) { effectSystem_ = s; }

    const std::vector<FxParticle>& particles() const { return particles_; }
    const std::vector<FxBeam>& beams() const { return beams_; }
    const std::vector<FxLight>& lights() const { return lights_; }
    const std::vector<FxLight>& persistentLights() const { return persistentLights_; }

private:
    std::vector<FxParticle> particles_;
    std::vector<FxBeam> beams_;
    std::vector<FxLight> lights_;
    std::vector<FxLight> persistentLights_;
    EffectSystem* effectSystem_ = nullptr;
};
