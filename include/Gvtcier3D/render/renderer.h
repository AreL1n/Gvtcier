#pragma once
#include <deque>
#include <map>
#include "bicu/core/conventions.h"
#include "bicu/core/shader.h"
#include "Rhidriver/rhi.h"
#include "bicu/core/BVH.h"
#include "bicu/core/OBB.h"
#include "Gvtcier3D/render/camera.h"
#include "Gvtcier3D/render/mesh.h"
#include "Gvtcier3D/render/texture.h"
#include "Gvtcier3D/render/shadowmap.h"
#include "Gvtcier3D/render/hdr.h"
#include "Gvtcier3D/render/fx.h"
#include "Gvtcier3D/render/effect.h"
#include "Gvtcier2D/render/font.h"
#include "Gvtcier3D/scene/scene.h"

struct RenderOptions {
    bool showShadow = true;
    bool hdr = true;
    bool wireframe = false;
    bool showShadowDebug = false;
    bool work = true;
    bool gtao = true;
    int gtaoSlices = 4;
    int gtaoSteps = 8;
    float gtaoRadius = 1.2f;
    float gtaoIntensity = 3.5f;
    float gtaoPower = 1.8f;
    float gtaoBias = 0.05f;
    float gtaoFalloff = 1.5f;
    float gtaoDenoiseBeta = 1.0f;
    bool debugCollider = false;
    int width = gv::kDefaultWindowWidth;
    int height = gv::kDefaultWindowHeight;

    void fromScene(const Scene& scene) {
        wireframe = scene.wireframe;
        showShadow = scene.showShadow;
        hdr = scene.hdr;
        showShadowDebug = scene.showShadowDebug;
        work = scene.volumeWork;
        gtao = scene.gtaoEnabled;
        gtaoSlices = scene.gtaoSlices;
        gtaoSteps = scene.gtaoSteps;
        gtaoRadius = scene.gtaoRadius;
        gtaoIntensity = scene.gtaoIntensity;
        gtaoPower = scene.gtaoPower;
        gtaoBias = scene.gtaoBias;
    }
};

struct DebugLineVertex {
    float pos[3];
    float r, g, b, a;
};

class Renderer {
public:
    GVTCIER_API bool Gvtcier_Render_Init();
    GVTCIER_API void Gvtcier_Render_Shutdown();
    GVTCIER_API void Gvtcier_Render_Render(Scene& scene, const Camera& cam, const RenderOptions& opt);
    // 按阶段分段跑同一帧：0 只做准备工作，1 天空，2 几何，3 后处理（含太阳阴影与光照合成），4 毛玻璃与 2D 画布。
    // 每段画完就退出，剩下的下一帧接着来；中途单独跑看着不完整，只在开场加载时用。
    GVTCIER_API void Gvtcier_Render_RenderPhase(Scene& scene, const Camera& cam, const RenderOptions& opt, int phase);
    GVTCIER_API static int Gvtcier_Render_LastPhase() { return 4; }
    GVTCIER_API FxSystem& Gvtcier_Render_Fx();
    GVTCIER_API void Gvtcier_Render_SetFlashlight(bool on);
    GVTCIER_API bool Gvtcier_Render_Flashlight() const { return flashlightOn_; }
    GVTCIER_API void Gvtcier_Render_SetDebugLines(const float* data, int floatCount);
    GVTCIER_API void Gvtcier_Render_ClearWorldLines();
    GVTCIER_API void Gvtcier_Render_AddWorldLine(float x1, float y1, float z1,
                                                 float x2, float y2, float z2,
                                                 float r, float g, float b, float a);
    GVTCIER_API void Gvtcier_Render_DrawText(int x, int y, float scale, const char* utf8, int z = 0, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    GVTCIER_API void Gvtcier_Render_DrawRect2D(float x, float y, float w, float h, float r, float g, float b, float a, int z = 0);
    GVTCIER_API void Gvtcier_Render_DrawImage(unsigned int tex, float x, float y, float w, float h, float a = 1.0f, int z = 0);
    GVTCIER_API void Gvtcier_Render_DrawGlass(float x, float y, float w, float h, float radius = 12.0f, float alpha = 0.72f, float tint = 0.06f, int z = 0);
    GVTCIER_API void Gvtcier_Render_BeginEngineUI();
    GVTCIER_API void Gvtcier_Render_EndEngineUI();
    GVTCIER_API void Gvtcier_Render_MeasureText(const char* utf8, float scale, float* outW, float* outH);
    GVTCIER_API void Gvtcier_Render_MeasureTextEx(const char* utf8, float scale, float* outW, float* outH, float* outTop);
    GVTCIER_API void Gvtcier_Render_FlushCanvas(int w, int h);
    GVTCIER_API void Gvtcier_Render_Render2D(int w, int h);
    GVTCIER_API void Gvtcier_Render_DrawNoSignal(int w, int h, const char* utf8);
    GVTCIER_API void Gvtcier_Render_Set2DMode(bool on);
    GVTCIER_API bool Gvtcier_Render_Is2DMode() const { return mode2D_; }
    GVTCIER_API EffectSystem& Gvtcier_Render_EffectSystem();
    GVTCIER_API GvtId<EffectAsset> Gvtcier_Render_EffectAssetFor(const std::string& path);
    GVTCIER_API void Gvtcier_Render_SyncSceneEffects(Scene& scene);
    GVTCIER_API void Gvtcier_Render_SetToon(int on);
    GVTCIER_API int Gvtcier_Render_GetToon() const;
    GVTCIER_API void Gvtcier_Render_SetOutlineWidth(float w);
    GVTCIER_API float Gvtcier_Render_GetOutlineWidth() const;
    GVTCIER_API void Gvtcier_Render_SetOutlineColor(float r, float g, float b);
    GVTCIER_API void Gvtcier_Render_GetOutlineColor(float* r, float* g, float* b) const;
    GVTCIER_API void Gvtcier_Render_ApplySettings(int shadowQuality, int anisotropic, bool specular,
                                                  bool autoExposure, int lightQuality, int fogQuality,
                                                  float vignetteStrength = 0.0f);
    GVTCIER_API void Gvtcier_Render_SetOcclusion(bool on);
    GVTCIER_API int Gvtcier_Render_GetOcclusion() const;
    GVTCIER_API int Gvtcier_Render_OccludedCount() const;

    typedef void (*Gvtcier_UserGlDrawFn)(void* user);
    GVTCIER_API void Gvtcier_Render_SetUserGlDraw(Gvtcier_UserGlDrawFn fn, void* user);
    GVTCIER_API void Gvtcier_Render_UserGlFrame(int width, int height);
    GVTCIER_API void Gvtcier_Render_SetRatio(int width, int height);
    GVTCIER_API void Gvtcier_Render_SetExposureMul(float mul) { exposureMul_ = mul; }
    GVTCIER_API void Gvtcier_Render_GetRatio(int* width, int* height) const;
    GVTCIER_API void Gvtcier_Render_SetFxaa(int on);
    GVTCIER_API int Gvtcier_Render_GetFxaa() const;
    GVTCIER_API void Gvtcier_Render_SetGameUIHidden(bool hidden);

private:
    void buildSkyLut(const Vec3& skyColor);
    void bakeVolNoise();
    void buildHzb(Scene& scene, const Camera& cam, const RenderOptions& opt);
    bool hzbTest(const AABB3& b, const Mat4& view, const Mat4& proj) const;
    void drawDebugLines(const Mat4& vp, const std::vector<DebugLineVertex>& verts);
    void volumetricShadowPass(Scene& scene);
    void sunShadowPass(Scene& scene, const Camera& cam, const Vec3& sunDirW, const RenderOptions& opt);
    void volumetricPass(Scene& scene, const Camera& cam, const RenderOptions& opt);
    void uploadCsmUniforms(Shader& sh, bool csmOn);
    void uploadAsmUniforms(Shader& sh, bool on);
    static constexpr int kAsmSlots = 16;
    static constexpr float kAsmMaxRadius = 8.0f;
    struct AsmCam { Mat4 vp; Vec3 center; float radius; float judge; float zLo; float zHi; float range; float texel; };
    AsmCam asmCams_[kAsmSlots];
    int asmCount_ = 0;
    ShadowMap asmShadow_;
    static constexpr int kPointShadowLights = 4;
    CubeShadowMap pointShadow_[kPointShadowLights];
    int pointShadowCount_ = 0;
    int pointShadowIdx_[kPointShadowLights];
    Vec3 pointShadowPos_[kPointShadowLights];
    float pointShadowFar_[kPointShadowLights];
    Shader pointDepthShader_;
    void shadowProbePass(const Camera& cam);
    void uploadDirLightUniforms(Shader& sh, const std::vector<Vec4>& dirs, const std::vector<Vec4>& cols);
    void renderCanvasItems2D();
    void glassPass(const RenderOptions& opt);
    void gtaoPass(const Scene& scene, const Camera& cam, const RenderOptions& opt, int rw, int rh);

    Gvtcier_Object<EffectAsset> effectAssets_;
    std::vector<std::pair<std::string, GvtId<EffectAsset>>> effectAssetIds_;
    std::map<uint64_t, int> effectPlaying_;
    GvtId<EffectAsset> effectAssetFor(const std::string& path);

    Shader mainShader_, depthShader_, fxShader_;
    Shader mainInstShader_;
    Shader deferredLightShader_;
    Shader gizmoShader_;
    Shader skinnedShader_;
    Shader depthSkinnedShader_;
    Shader brightShader_, blurShader_, composeShader_, skyShader_;
    Shader glassShader_;
    Shader lumShader_;
    ColorFbo hzbCopyFbo_;
    bool volShadowDirty_ = true;
    unsigned lastVolSig_ = 0;
    Vec3 lastVolLightDir_;
    rhi::Buffer probePbo_[2] = {0, 0};
    int probePboIndex_ = 0;
    bool probePboPending_ = false;
    bool lumPboPending_ = false;
    int lumPboIndex_ = 0;
    double exposureLastTime_ = 0.0;
    Shader fxaaShader_;
    Shader gtaoShader_, gtaoBlurShader_;
    Shader volShader_, volBlurShader_;
    Shader outlineShader_;
    Shader outlineSkinnedShader_;
    bool toonOn_ = false;
    bool fxaaOn_ = true;
    float outlineW_ = 0.04f;
    float outlineCol_[3] = { 0.0f, 0.0f, 0.0f };
    bool specularOn_ = true;
    bool autoExposureOn_ = false;
    float vignette_ = 0.3f;
    bool flashlightOn_ = false;
    int lightLimit_ = 256;
    int fogLevel_ = 1;
    int shadowSize_ = gv::kShadowMapSize;
    ShadowMap volShadow_;
    ShadowMap sunShadow_;
    Mat4 csmVP_[4];
    Vec3 csmAnchor_{0.0, 0.0, 0.0};
    float csmSplits_[4] = {8.0f, 24.0f, 64.0f, 160.0f};
    float csmTexel_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float csmRange_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    int csmNoiseFrame_ = 0;
    unsigned int volNoiseTex_ = 0;
    unsigned int volFbo_[2] = {0, 0};
    unsigned int volTex_[2] = {0, 0};
    int volW_ = 0, volH_ = 0;
    Mat4 volLightVp_;
    bool volLightValid_ = false;
    int volFrame_ = 0;
    bool volDebugRed_ = false;
    Shader textShader_;
    Shader imageShader_;
    Shader rect2dShader_;
    ColorFbo hdrFbo_;
    ColorFbo bloomMips_[4];
    ColorFbo glassFbo_[4];
    unsigned int glassSceneTex_ = 0;
    int glassSceneW_ = 0, glassSceneH_ = 0;
    struct GlassPanel {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float radius = 12.0f, alpha = 0.72f, tint = 0.06f;
        int z = 0;
    };
    std::vector<GlassPanel> glassPanels_;
    ColorFbo lumFbo_;
    ColorFbo ldrFbo_;
    ColorFbo probeFbo_;
    GvtId<Object> probeLastObj_;
    ColorFbo gtaoFbo_[3];
    float gtaoLastW_ = 0.0f, gtaoLastH_ = 0.0f;
    unsigned int gtaoAOTex_ = 0;
    float exposureSmooth_ = 0.26f;
    bool exposureInit_ = false;
    float exposureMul_ = 1.0f;
    Mesh skySphere_;
    unsigned int skyGradLut_ = 0;
    Vec3 skyLutColor_ = Vec3(-1.0f, -1.0f, -1.0f);
    float sunOnSmooth_ = 1.0f;
    Mesh fullscreenQuad_;
    Bvh bvh_;
    std::vector<int> visibleList_;
    std::vector<AABB3> objBoundsBuf_;
    std::vector<GvtId<Object>> objIndexBuf_;
    std::vector<AABB3> hzbBounds_;
    std::vector<unsigned char> hzbReadBuf_;
    std::vector<float> hzbDepth_;
    rhi::Buffer hzbPbo_[2] = {0, 0};
    int hzbPboIndex_ = 0;
    bool hzbPboPending_ = false;
    Mat4 hzbProj_;
    Mat4 hzbView_;
    int hzbW_ = 0, hzbH_ = 0;
    bool hzbValid_ = false;
    bool occlusionOn_ = true;
    int occludedCount_ = 0;
    std::vector<float> instScratch_;
    struct InstBucket {
        Mesh* mesh = nullptr;
        Texture* tex = nullptr;
        Texture* tex6 = nullptr;
        std::vector<const Object*> objs;
        bool useScissor = false;
        int sx = 0, sy = 0, sw = 0, sh = 0;
    };
    std::vector<InstBucket> instBuckets_;
    std::vector<const Object*> instOrder_;
    unsigned int fxVao_ = 0, fxVbo_ = 0;
    unsigned int dbgLineVao_ = 0, dbgLineVbo_ = 0;
    std::vector<DebugLineVertex> dbgLines_;
    bool dbgLinesDirty_ = false;
    std::vector<DebugLineVertex> worldLines_;
    unsigned int lightSsbo_ = 0, tileSsbo_ = 0, idxSsbo_ = 0;
    int tileW_ = 0, tileH_ = 0;
    std::vector<float> lightDataBuf_;
    std::vector<unsigned int> tileDataBuf_;
    std::vector<unsigned int> lightIdxBuf_;
    std::vector<unsigned int> tileCountBuf_;
    std::vector<unsigned int> tileCursorBuf_;
    struct TileRange { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool all = false; };
    std::vector<TileRange> lightTileRanges_;
    FxSystem fx_;
    EffectSystem effectSystem_;
    FontAtlas font_;
    struct TextCacheEntry {
        std::string text;
        float scale = 0.0f;
        std::vector<float> body;
        std::vector<int> pages;
    };
    std::vector<TextCacheEntry> textCache_;
    unsigned int textVao_ = 0, textVbo_ = 0;
    struct TextEntityTex {
        std::string text;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        int w = 0, h = 0;
        GvtId<Texture> tex;
    };
    std::map<std::string, TextEntityTex> textEntityTexs_;
    struct CanvasPrim {
        enum Type { Rect, Text, Image };
        Type type = Rect;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
        int z = 0;
        float scale = 1.0f;
        unsigned int tex = 0;
        std::string text;
    };
    void pushCanvasPrim(const CanvasPrim& p);
    bool suppressGameUI_ = false;
    void flushQueue(std::vector<CanvasPrim>& items, int w, int h);
    std::vector<CanvasPrim> canvasItems_;
    std::vector<CanvasPrim> engineItems_;
    bool writeEngine_ = false;
    unsigned int rect2dVao_ = 0, rect2dVbo_ = 0;
    int viewW_ = 0, viewH_ = 0;
    int renderW_ = 0, renderH_ = 0;
    // 本帧跑到第几段了，分段加载靠它接着往下走
    int phase_ = 0;
    // 本帧要画到第几段为止。整帧渲染时是最后一段，于是中途一个门都不会关
    int untilPhase_ = 4;
    bool Gvtcier_StageOver() const { return phase_ > untilPhase_; }
    void renderRes(int& w, int& h, const RenderOptions& opt) const {
        w = renderW_ > 0 ? renderW_ : opt.width;
        h = renderH_ > 0 ? renderH_ : opt.height;
        if (w < 1) w = 1;
        if (h < 1) h = 1;
    }
    bool mode2D_ = false;
    Gvtcier_UserGlDrawFn userGlDraw_ = nullptr;
    void* userGlUser_ = nullptr;
};
