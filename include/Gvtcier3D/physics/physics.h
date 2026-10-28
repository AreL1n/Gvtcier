#pragma once
#include <vector>
#include "Gvtcier3D/scene/scene.h"
#include "Gvtcier3D/render/camera.h"
#include "bicu/core/Player.h"

class PhysicsWorld {
public:
    GVTCIER_API bool Gvtcier_Physics_Init(const Scene& scene, const Player& p, const PlayerSettings& ps);
    GVTCIER_API void Gvtcier_Physics_Shutdown();

    GVTCIER_API void Gvtcier_Physics_Step(Player& p, Scene& scene, float dt);

    GVTCIER_API bool Gvtcier_Physics_PlayerGrounded() const;

    GVTCIER_API Vec3 Gvtcier_Physics_PlayerVelocity() const;

    GVTCIER_API bool Gvtcier_Physics_AddObject(const Object& o);
    GVTCIER_API bool Gvtcier_Physics_AddObject(const Object& o, GvtId<Object> id);
    GVTCIER_API void Gvtcier_Physics_RemoveObject(GvtId<Object> id);
    GVTCIER_API void Gvtcier_Physics_RemoveObjects(const std::vector<GvtId<Object>>& ids);
    GVTCIER_API void Gvtcier_Physics_RebuildSkinned(const Scene& scene);
    GVTCIER_API void Gvtcier_Physics_SetObjectPos(GvtId<Object> id, const Vec3& pos);
    GVTCIER_API void Gvtcier_Physics_SetObjectVelocity(GvtId<Object> id, const Vec3& vel);
    GVTCIER_API void Gvtcier_Physics_SetObjectAngularVelocity(GvtId<Object> id, const Vec3& angVel);
    GVTCIER_API void Gvtcier_Physics_FollowObject(GvtId<Object> id, const Vec3& pos, const Vec3& rotDeg, const Vec3& scale);
    GVTCIER_API void Gvtcier_Physics_SyncChildren(const Scene& scene);
    GVTCIER_API void Gvtcier_Physics_SetKinematic(GvtId<Object> id, const Vec3& vel, const Vec3& angVel, const Vec3& pivot);
    GVTCIER_API bool Gvtcier_Physics_PlayerOnPlatform() const;
    GVTCIER_API void Gvtcier_Physics_SetPlayerSpeed(float v);
    GVTCIER_API void Gvtcier_Physics_SetWalkRunSpeed(float walk, float run);
    GVTCIER_API void Gvtcier_Physics_GetWalkRunSpeed(float* walk, float* run) const;
    GVTCIER_API void Gvtcier_Physics_SetRunEnabled(bool on);
    GVTCIER_API bool Gvtcier_Physics_RunEnabled() const;
    GVTCIER_API void Gvtcier_Physics_SetPlayerJump(float v);
    GVTCIER_API void Gvtcier_Physics_SetPlayerPos(const Vec3& pos);
    GVTCIER_API GvtId<Object> Gvtcier_Physics_CastRay(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDist);
    GVTCIER_API GvtId<Object> Gvtcier_Physics_CastRayEx(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDist,
                                              Vec3* outPoint, Vec3* outNormal, float* outDist);
    GVTCIER_API bool Gvtcier_Physics_CastRaySurface(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDist,
                                                   Vec3* outPoint, Vec3* outNormal, float* outDist);

    GVTCIER_API int Gvtcier_Physics_CollectWireframe(float* out, int maxFloats) const;

    GVTCIER_API bool Gvtcier_Physics_CreateSensor(const Vec3& center, const Vec3& half, bool useSphere);
    GVTCIER_API void Gvtcier_Physics_DestroySensor();
    GVTCIER_API int Gvtcier_Physics_QuerySensor(std::vector<GvtId<Object>>& out) const;
    GVTCIER_API bool Gvtcier_Physics_OverlapShape(const Vec3& center, const Vec3& half, bool useSphere,
                                                  std::vector<GvtId<Object>>& out) const;

private:
    void* impl_ = nullptr;
};
