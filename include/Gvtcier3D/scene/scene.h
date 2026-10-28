#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "bicu/core/math.h"
#include "bicu/core/conventions.h"
#include "bicu/core/export.h"
#include "bicu/core/AABB.h"
#include "bicu/core/identity.h"

class Mesh;
class Texture;
struct SkinnedMesh;

class PhysicsWorld;
class EffectSystem;
class SceneMeshes;
class SceneSkinned;

struct Light {
    Vec3 dir = Vec3(0.5f, -1.0f, 0.3f).normalized();
    Vec3 color = Vec3(1.0f, 0.95f, 0.88f);
};

enum class AnimEase { Linear, EaseIn, EaseOut, EaseInOut, Step };

struct AnimTrack {
    std::string key;
    int comp = 0;
    double from = 0.0;
    double to = 0.0;
    float dur = 0.3f;
    float cur = 0.0f;
    AnimEase ease = AnimEase::EaseInOut;
    bool started = false;
};

struct AnimState {
    std::string name;
    bool loop = false;
    float hold = 0.0f;
    std::vector<AnimTrack> tracks;
};

enum class ColliderType { None, Box, Sphere, Capsule, Cylinder, ConvexHull, Mesh };
enum class ObjKind { Geometry, LightPoint, LightDir, LightSpot, Trigger, Audio, Image, Effect, EffectEmitter, Player, Camera, Text, Model };

struct Object {
    std::string name;
    std::string uid;
    std::string tag;
    ObjKind kind = ObjKind::Geometry;
    GvtId<Mesh> mesh;
    GvtId<Texture> tex;
    Vec3 pos;
    Vec3 rotDeg;
    Vec3 scale = Vec3(1, 1, 1);
    Vec3 pivot;
    GvtId<Object> parent;
    Vec3 localPos;
    Vec3 localRotDeg;
    Vec3 localScale = Vec3(1, 1, 1);
    bool xformDirty = true;
    bool staticXform = false;
    mutable bool matrixDirty = true;
    mutable bool boundsDirty = true;
    mutable Mat4 cachedMatrix;
    mutable AABB3 cachedBounds;

    void Gvtcier_Object_MarkXformDirty() {
        matrixDirty = true;
        boundsDirty = true;
        xformDirty = true;
    }
    void Gvtcier_Object_MarkCacheDirty() {
        matrixDirty = true;
        boundsDirty = true;
    }
    const Mat4& Gvtcier_Object_Matrix() const;
    const AABB3& Gvtcier_Object_Bounds(const Vec3& half) const;
    float shininess = 32.0f;
    ColliderType collider = ColliderType::None;
    bool pushable = false;
    bool rigidBody = false;
    bool physics = false;
    Vec3 vel;
    Vec3 angVel;
    float mass = 1.0f;
    float friction = 0.6f;
    bool kinematic = false;
    Vec3 kinVel;
    Vec3 kinAngVel;
    Vec3 kinPivot;
    int material = 0;
    bool specular = true;
    float uvScale = 1.0f;
    std::string texName;
    GvtId<Texture> tex6;
    std::string tex6Name;
    bool visible = true;
    bool transparent = false;
    float alpha = 1.0f;
    bool castShadow = true;
    bool lightShadow = true;
    Vec3 lightColor = Vec3(1.0f, 0.95f, 0.88f);
    float lightIntensity = 1.0f;
    float lightRange = 8.0f;
    float spotAngle = 35.0f;
    std::string scriptPath;
    float fov = gv::kDefaultFov;
    Vec3 albedoTint = Vec3(0.95f, 0.93f, 0.88f);
    float moveSpeed = 0.0f;
    float jumpHeight = 1.5f;
    float nearPlane = 0.05f;
    float farPlane = 2000.0f;
    float volume = 1.0f;
    float soundRange = 12.0f;
    std::string audioPath;
    float pitch = 1.0f;
    bool spatial = true;
    bool attenuation = true;
    std::string imageName;
    std::string text;
    std::string modelName;
    float textSize = 1.0f;
    int effectType = 0;
    float effectParam0 = 1.0f;
    float effectParam1 = 1.0f;
    std::string effectAsset;
    float effectDir = 0.0f;
    float effectEmissive = 0.0f;
    bool autoPlay = false;
    bool loop = true;
    Vec3 laserA = Vec3(0.0f, 0.0f, 0.0f);
    Vec3 laserB = Vec3(2.0f, 0.0f, 0.0f);
    float laserWidth = 0.08f;
    GVTCIER_API Vec3 Gvtcier_Object_MeshHalfExtents() const;
};

struct AnimStateMachine {
    std::string name;
    GvtId<Object> target;
    std::vector<AnimState> states;
    int current = -1;
    int queued = -1;
    bool playing = false;
    bool loopDefault = false;
    float speed = 1.0f;
};

class Scene {
public:
    GVTCIER_API Scene();
    GVTCIER_API ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    GVTCIER_API bool Gvtcier_Scene_Build();
    GVTCIER_API void Gvtcier_Scene_InitSkybox();
    GVTCIER_API void Gvtcier_Scene_InitSun();
    GVTCIER_API void Gvtcier_Scene_InitVolumeLight();
    GVTCIER_API void Gvtcier_Scene_InitFog();
    GVTCIER_API void Gvtcier_Scene_CaptureLook();
    GVTCIER_API void Gvtcier_Scene_RestoreLook();
    GVTCIER_API bool Gvtcier_Scene_BuildPlatformer();
    GVTCIER_API void Gvtcier_Scene_Destroy();

    GVTCIER_API void Gvtcier_Scene_AddObject(const Object& o);
    GVTCIER_API GvtId<Object> Gvtcier_Scene_RegisterObject(Object&& o);
    GVTCIER_API static std::string Gvtcier_Scene_GenerateUid(const char* prefix);
    GVTCIER_API const char* Gvtcier_Scene_ObjectUid(GvtId<Object> id);
    GVTCIER_API GvtId<Object> Gvtcier_Scene_FindByUid(const char* uid);

    GvtId<Mesh> cubeMesh();
    GvtId<Mesh> planeMesh();
    GvtId<Mesh> sphereMesh();
    GvtId<Mesh> coneMesh();
    GvtId<Mesh> planeCircleMesh();
    GvtId<Mesh> planeTriMesh();
    GvtId<Mesh> cylinderMesh();
    GvtId<Mesh> torusMesh();
    GvtId<Mesh> colliderBoxMesh();
    GvtId<Mesh> colliderCylinderMesh();
    GVTCIER_API GvtId<Mesh> Gvtcier_Scene_LoadModel(const char* resName);
    GVTCIER_API GvtId<Mesh> Gvtcier_Scene_LoadModelFile(const char* path);
    GVTCIER_API GvtId<Mesh> Gvtcier_Scene_LoadModelObject(const char* path);
    GVTCIER_API GvtId<Texture> Gvtcier_Scene_LoadPngTexture(const char* path);
    GVTCIER_API GvtId<Texture> Gvtcier_Scene_LoadPngRes(const char* resName);
    // 开场预载用：把贴图真的解出来并报结果。失败和 LoadPngRes 一样退回白图，但这里能分辨出来。
    GVTCIER_API bool Gvtcier_Scene_PreloadTex(const char* resName);
    GVTCIER_API GvtId<Texture> Gvtcier_Scene_BuiltinTexture(const char* name);
    GVTCIER_API bool Gvtcier_Scene_LoadGmap(const char* gmapPath);
    GVTCIER_API bool Gvtcier_Scene_HasBuiltinTexture(const char* name) const;
    GVTCIER_API const char* Gvtcier_Scene_BuiltinTextureName(int i) const;
    GVTCIER_API int Gvtcier_Scene_BuiltinTextureCount() const;
    GvtId<Texture> whiteTex();
    GVTCIER_API GvtId<Texture> Gvtcier_Scene_LoadPngArrayRes(const char* const* resNames, int n);
    GVTCIER_API const char* Gvtcier_Scene_BuiltinResName(const char* name);

    GVTCIER_API bool Gvtcier_Scene_LoadSkinnedModel(const char* resName, const Vec3& pos = Vec3(0, 0, 0));
    GVTCIER_API bool Gvtcier_Scene_LoadSkinnedFile(const char* path, const Vec3& pos = Vec3(0, 0, 0));
    GVTCIER_API bool hasSkinned() const;
    GVTCIER_API SkinnedMesh& skinnedMesh();
    GVTCIER_API const SkinnedMesh& skinnedMesh() const;
    GvtId<Texture> skinnedTex();
    GVTCIER_API Mesh* Gvtcier_Scene_GetSkinnedMesh();
    GVTCIER_API const Mesh* Gvtcier_Scene_GetSkinnedMesh() const;
    GVTCIER_API Texture* Gvtcier_Scene_GetSkinnedTex();
    GVTCIER_API const Texture* Gvtcier_Scene_GetSkinnedTex() const;
    std::vector<float> skinnedBoneMats;

    GVTCIER_API int Gvtcier_Scene_AnimCount() const;
    GVTCIER_API const char* Gvtcier_Scene_AnimName(int i) const;
    GVTCIER_API float Gvtcier_Scene_AnimDuration(int i) const;
    GVTCIER_API bool Gvtcier_Scene_AnimPlay(int i, bool loop = true);
    GVTCIER_API void Gvtcier_Scene_AnimStop();
    GVTCIER_API bool Gvtcier_Scene_AnimPlaying() const;
    GVTCIER_API void Gvtcier_Scene_AnimSetSpeed(float s);
    GVTCIER_API float Gvtcier_Scene_AnimSpeed() const;
    GVTCIER_API void Gvtcier_Scene_AnimSetTime(float t);
    GVTCIER_API float Gvtcier_Scene_AnimTime() const;
    GVTCIER_API void Gvtcier_Scene_AnimSetPos(const Vec3& p);
    GVTCIER_API Vec3 Gvtcier_Scene_AnimPos() const;
    GVTCIER_API void Gvtcier_Scene_AnimTick(float dt);

    GVTCIER_API int Gvtcier_Scene_AnimSmCreate(const char* name, GvtId<Object> target);
    GVTCIER_API int Gvtcier_Scene_AnimSmFind(const char* name) const;
    GVTCIER_API AnimStateMachine* Gvtcier_Scene_AnimSm(int id);
    GVTCIER_API int Gvtcier_Scene_AnimSmStateId(AnimStateMachine* sm, const char* stateName);
    GVTCIER_API void Gvtcier_Scene_AnimSmAddState(int id, const char* stateName, bool loop, float hold);
    GVTCIER_API void Gvtcier_Scene_AnimSmAddTrack(int id, const char* stateName, const char* key, int comp, double to, float dur, AnimEase ease);
    GVTCIER_API bool Gvtcier_Scene_AnimSmSet(int id, const char* stateName, bool immediate);
    GVTCIER_API void Gvtcier_Scene_AnimSmSetSpeed(int id, float speed);
    GVTCIER_API const char* Gvtcier_Scene_AnimSmCurrent(int id) const;
    GVTCIER_API void Gvtcier_Scene_AnimSmTick(float dt);

    GVTCIER_API Vec3 Gvtcier_Scene_MeshHalfExtents(GvtId<Mesh> mesh) const;
    GVTCIER_API GvtId<Mesh> Gvtcier_Scene_PlaneMesh() const { return planeMeshId_; }
    GVTCIER_API GvtId<Mesh> Gvtcier_Scene_ImageMesh() const { return imageMeshId_; }

    GVTCIER_API Mesh* Gvtcier_Scene_GetMesh(GvtId<Mesh> id);
    GVTCIER_API const Mesh* Gvtcier_Scene_GetMesh(GvtId<Mesh> id) const;
    GVTCIER_API Texture* Gvtcier_Scene_GetTex(GvtId<Texture> id);
    GVTCIER_API const Texture* Gvtcier_Scene_GetTex(GvtId<Texture> id) const;
    GVTCIER_API GvtId<Texture> Gvtcier_Scene_RegisterTexture(Texture&& tex);
    GVTCIER_API void Gvtcier_Scene_UnregisterTexture(GvtId<Texture> id);

    GVTCIER_API void Gvtcier_Scene_RefreshTransforms();

    GVTCIER_API Object* Gvtcier_Scene_GetObject(GvtId<Object> id);
    GVTCIER_API const Object* Gvtcier_Scene_GetObject(GvtId<Object> id) const;
    GVTCIER_API GvtId<Object> Gvtcier_Scene_IdOf(const Object* o) const;
    GVTCIER_API bool Gvtcier_Scene_SetParent(GvtId<Object> child, GvtId<Object> parent);
    GVTCIER_API void Gvtcier_Scene_DetachChildren(GvtId<Object> id);

    GVTCIER_API bool Gvtcier_Scene_IsAncestor(GvtId<Object> ancestor, GvtId<Object> id) const;

    GVTCIER_API GvtId<Object> Gvtcier_Scene_FindByName(const char* name);
    GVTCIER_API size_t Gvtcier_Scene_ObjectCount() const;
    GVTCIER_API std::vector<GvtId<Object>> Gvtcier_Scene_ObjectIds() const;
    template<typename Fn>
    void ForEachObject(Fn fn) { objects_.ForEachAlive(fn); }
    template<typename Fn>
    void ForEachObject(Fn fn) const { objects_.ForEachAlive(fn); }
    GVTCIER_API void Gvtcier_Scene_InvalidateNameIndex();

    GVTCIER_API void Gvtcier_Scene_ClearObjects();

    Light light;
    float gravity = 30.0f;
    float timeScale = 1.0f;
    bool paused = false;
    bool inputLocked = false;
    std::vector<GvtId<Object>> pendingRemoves;
    void queueRemove(GvtId<Object> id) { if (id.valid()) pendingRemoves.push_back(id); }
    GVTCIER_API void Gvtcier_Scene_FlushRemoves(PhysicsWorld* physics);
    bool sunEnabled = false;
    bool ambientEnabled = false;
    bool volumeLightEnabled = true;
    bool volumetricFogEnabled = false;
    float volFogDensity = 0.06f;
    float volFogHeight = 0.055f;
    float volFogHeightBase = -2.0f;
    float volFogNoise = 0.5f;
    float volFogNoiseScale = 0.3f;
    float volFogScatter = 0.5f;
    int volFogSteps = 32;
    Vec3 volFogColor = Vec3(0.75f, 0.80f, 0.88f);
    float ambientIntensity = 0.5f;
    Vec3 ambientColor = Vec3(1.0f, 1.0f, 1.0f);
    float volStrength = 0.4f;
    float bloomStrength = 0.65f;
    Vec3 skyColor = Vec3(0.62f, 0.78f, 0.96f);
    bool showSkybox = true;
    float fogDensity = 0.004f;
    bool hdr = true;
    bool wireframe = false;
    bool showShadow = true;
    bool showShadowDebug = false;
    bool volumeWork = true;
    bool gtaoEnabled = true;
    int gtaoSlices = 4;
    int gtaoSteps = 10;
    float gtaoRadius = 1.2f;
    float gtaoIntensity = 3.5f;
    float gtaoPower = 1.8f;
    float gtaoBias = 0.05f;
    bool showCrosshair = true;
    bool captureMouse = true;
    std::string mainCamera;

    int animIndex_ = -1;
    bool animPlaying_ = false;
    bool animLoop_ = true;
    float animTime_ = 0.0f;
    float animSpeed_ = 1.0f;
    bool animPoseDirty_ = true;
    Vec3 skinnedPos_ = Vec3(0, 0, 0);

private:
    std::unique_ptr<SceneMeshes> meshes_;
    std::unique_ptr<SceneSkinned> skinned_;

    Gvtcier_Object<Object> objects_;

    Gvtcier_Object<Mesh> meshStore_;
    Gvtcier_Object<Texture> texStore_;

    GvtId<Mesh> cubeMeshId_, planeMeshId_, sphereMeshId_, coneMeshId_;
    GvtId<Mesh> planeCircleMeshId_, planeTriMeshId_, cylinderMeshId_, torusMeshId_;
    GvtId<Mesh> colliderBoxMeshId_, colliderCylinderMeshId_;
    GvtId<Mesh> objMeshId_, gltfMeshId_, fileMeshId_;
    GvtId<Mesh> imageMeshId_;
    GvtId<Texture> whiteTexId_;
    GvtId<Texture> skinnedTexId_;

    std::unordered_map<std::string, GvtId<Object>> nameIndex_;
    mutable std::unordered_map<std::string, GvtId<Object>> uidIndex_;
    mutable bool uidIndexValid_ = false;
    bool nameIndexValid_ = false;

    std::vector<AnimStateMachine> animSms_;

    struct LookSnapshot {
        Vec3 skyColor = Vec3(0.62f, 0.78f, 0.96f);
        bool showSkybox = true;
        bool sunEnabled = false;
        Light light;
        bool volumeLightEnabled = true;
        float volStrength = 0.4f;
        bool volumetricFogEnabled = false;
        float fogDensity = 0.004f;
        float bloomStrength = 0.65f;
        bool hdr = true;
        bool gtaoEnabled = true;
        bool ambientEnabled = false;
        float ambientIntensity = 0.5f;
        Vec3 ambientColor = Vec3(1.0f, 1.0f, 1.0f);
        bool valid = false;
    };
    LookSnapshot look_;
};
