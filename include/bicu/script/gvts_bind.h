#pragma once
#include <string>
#include <vector>
#include <map>
#include "bicu/core/export.h"
#include "Gvtcier3D/scene/scene.h"
#include "Gvtcier3D/physics/physics.h"
#include "Gvtcier3D/render/camera.h"
#include "bicu/platform/gvw.h"

class FxSystem;
class Renderer;

#ifdef GVWCIER_USE_GLES
class Window {
public:
    explicit Window(GvWWindow* w = nullptr) : gw_(w) {}
    int width() const { return gw_ ? GvW_Width(gw_) : 0; }
    int height() const { return gw_ ? GvW_Height(gw_) : 0; }
    int mode() const { return 3; }
    bool focused() const { return true; }
    bool isKeyDown(int k) const { return gw_ ? GvW_IsKeyDown(gw_, k) : false; }
    int Gvtcier_Window_ConsumeKeyPress() { return gw_ ? GvW_ConsumeKeyPress(gw_) : 0; }
    int Gvtcier_Window_ConsumeScroll() { return 0; }
    void Gvtcier_Window_MouseDelta(int* dx, int* dy) { if (gw_) GvW_MouseDelta(gw_, dx, dy); else { if (dx) *dx = 0; if (dy) *dy = 0; } }
    void Gvtcier_Window_CursorPos(int& x, int& y) const { if (gw_) GvW_MousePos(gw_, &x, &y); else { x = 0; y = 0; } }
    bool Gvtcier_Window_GrabMouse(bool) { return false; }
    GvWWindow* gw_ = nullptr;
};
#else
class Window;
#endif

struct GvtsTimer {
    double remain = 0.0;
    int ref = -1;
    bool hasFn = false;
    void* thread = nullptr;
};

struct GvtsCo {
    void* thread = nullptr;
    double remain = 0.0;
    int ref = -1;
};

struct GvtsTween {
    int targetKind = 0;
    GvtId<Object> obj;
    std::string key;
    int propId = 0;
    double from = 0.0;
    double to = 0.0;
    double dur = 0.0;
    double cur = 0.0;
    int comp = 0;
};

struct GvtsEngineCtx {
    Scene* scene = nullptr;
    PhysicsWorld* phys = nullptr;
    Player* player = nullptr;
    Camera* cam = nullptr;
    class Window* win = nullptr;
    FxSystem* fx = nullptr;
    Renderer* renderer = nullptr;
    class Canvas2D* canvas2d = nullptr;
    bool onlyGl = false;
    bool inputFrozen = false;
    void* ownerHandle = nullptr;
    void* mainState = nullptr;
    float lastDt = 1.0f / 60.0f;
    double elapsed = 0.0;
    int frameCount = 0;
    std::vector<GvtsTimer> timers;
    std::vector<GvtsTween> tweens;
    std::vector<GvtsCo> waitCos;
    std::map<std::string, int> trigEnterCb;
    std::map<std::string, int> trigExitCb;
    struct Broadcast {
        int cbRef = 0;
        bool pending = false;
        char payload[256] = "";
    };
    std::map<std::string, Broadcast> broadcasts;
};

GVTCIER_API const std::string& Gvtcier_Gvts_AttrCode();
GVTCIER_API void Gvtcier_Gvts_ClearAttr();

class GvtsBind {
public:
    GVTCIER_API ~GvtsBind();
    GVTCIER_API bool init();
    GVTCIER_API bool run(const std::string& code);
    GVTCIER_API bool call(const std::string& fn);
    GVTCIER_API bool loadGame(const char* path);
    GVTCIER_API void tick(Scene& scene, float dt);
    GVTCIER_API void draw(float dt = 0.0f);
    GVTCIER_API void shutdown();
    void setScene(Scene* s) { ctx_.scene = s; }
    void setPhysics(PhysicsWorld* p) { ctx_.phys = p; }
    void setPlayer(Player* p) { ctx_.player = p; }
    void setCamera(Camera* c) { ctx_.cam = c; }
    void setWindow(class Window* w) { ctx_.win = w; }
    void setFx(FxSystem* f) { ctx_.fx = f; }
    void setRenderer(Renderer* r) { ctx_.renderer = r; }
    void setCanvas2D(class Canvas2D* c) { ctx_.canvas2d = c; }
    void* state() { return state_; }

    Scene* scene() { return ctx_.scene; }
    PhysicsWorld* physics() { return ctx_.phys; }
    Player* player() { return ctx_.player; }
    Camera* camera() { return ctx_.cam; }
    Window* window() { return ctx_.win; }
    FxSystem* fx() { return ctx_.fx; }
    Renderer* renderer() { return ctx_.renderer; }
    Canvas2D* canvas2d() { return ctx_.canvas2d; }
    void setOnlyGl(bool v) { ctx_.onlyGl = v; }
    bool isOnlyGl() const { return ctx_.onlyGl; }
    void setInputFrozen(bool v) { ctx_.inputFrozen = v; }
    GVTCIER_API void tickObjectScripts(Scene& scene, float dt);
    GVTCIER_API void drawObjectScripts();
    void setOwnerHandle(void* h) { ctx_.ownerHandle = h; }

private:
    void applyMainLoopMode();
    const char* mainLoopName() const;
    bool loadModScript();
    void callHook(const char* name);
    void* state_ = nullptr;
    bool gvtEnabled_ = false;
    bool modLoaded_ = false;
    bool hookLoaded_ = false;
    std::string chunkName_ = "game.gvts";
    GvtsEngineCtx ctx_;
    std::vector<int> objScriptRefs_;
    std::vector<GvtId<Object>> objScriptObj_;
    std::vector<std::string> objScriptPath_;
    std::vector<void*> pendingThreads_;
    void* tickCo_ = nullptr;
    void* drawCo_ = nullptr;
    bool resumeCo(void* thread);
    void* startCo(const std::string& fn, int nargs);
};
