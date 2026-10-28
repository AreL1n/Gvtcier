#pragma once
#include <string>
#include <deque>
#include "bicu/core/export.h"
#include "bicu/core/conventions.h"

inline constexpr int kDefaultNetPort = 22800;

enum FogQuality {
    FOG_NONE = 0,
    FOG_LOW = 1,
    FOG_MEDIUM = 2,
    FOG_HIGH = 3
};

enum LightQuality {
    LIGHT_LOW = 0,
    LIGHT_MEDIUM = 1,
    LIGHT_HIGH = 2
};

enum ShadowQuality {
    SHADOW_OFF = 0,
    SHADOW_LOW = 1,
    SHADOW_MEDIUM = 2,
    SHADOW_HIGH = 3
};

enum KeyPreset {
    PRESET_LEFT_HAND = 0,
    PRESET_RIGHT_HAND = 1,
    PRESET_CUSTOM = 2
};

enum WindowMode {
    WINDOW_FULLSCREEN = 0,
    WINDOW_BORDERLESS = 1,
    WINDOW_MAXIMIZED = 2,
    WINDOW_WINDOWED = 3
};

enum KeyAction {
    ACTION_FORWARD = 0,
    ACTION_BACK,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_JUMP,
    ACTION_SPRINT,
    ACTION_CROUCH,
    ACTION_COUNT
};

struct GvtKeyAction {
    std::string id;
    std::string name;
    int vk = 0;
};

enum FrameRateMode {
    FRAMERATE_VSYNC = 0,
    FRAMERATE_30 = 1,
    FRAMERATE_60 = 2,
    FRAMERATE_90 = 3,
    FRAMERATE_120 = 4,
    FRAMERATE_UNLIMITED = 5
};

struct GameSettings {
    float masterVolume = 1.0f;
    float soundVolume = 1.0f;
    float musicVolume = 1.0f;
    bool motionCamera = true;
    float fov = gv::kDefaultFov;
    int frameRate = FRAMERATE_VSYNC;
    int windowMode = WINDOW_WINDOWED;

    bool hdr = true;
    float bloomStrength = 0.65f;
    int anisotropic = 2;
    int shadowQuality = SHADOW_MEDIUM;
    bool volumeLight = true;
    bool autoExposure = true;
    int fogQuality = FOG_MEDIUM;
    bool specular = true;
    int lightQuality = LIGHT_HIGH;
    float vignetteStrength = 0.7f;
    bool gtao = true;
    bool fxaa = true;
    bool occlusion = true;

    float mouseSensitivity = 1.0f;
    int keyPreset = PRESET_LEFT_HAND;
    int keyBindings[ACTION_COUNT] = { 'W', 'S', 'A', 'D', 0x20, 0xA0, 0xA2 };
    int menuKey = 0x1B;

    int netPort = kDefaultNetPort;
    std::string serverRoom = "gvtcier";

    std::deque<GvtKeyAction> actions;
};

GVTCIER_API GameSettings& Gvtcier_Settings();
GVTCIER_API void Gvtcier_Settings_Load(const char* gameDir);
GVTCIER_API void Gvtcier_Settings_Save();
GVTCIER_API void Gvtcier_Settings_ApplyPreset(int preset);
GVTCIER_API const char* Gvtcier_Settings_ActionName(int action);
GVTCIER_API std::string Gvtcier_Settings_KeyName(int vk);
GVTCIER_API int Gvtcier_Settings_RegisterAction(const char* id, const char* name, int defaultVk);
GVTCIER_API int Gvtcier_Settings_FindAction(const char* id);
GVTCIER_API int Gvtcier_Settings_ActionCount();
GVTCIER_API const char* Gvtcier_Settings_ActionIdAt(int i);
GVTCIER_API const char* Gvtcier_Settings_ActionNameAt(int i);
GVTCIER_API int Gvtcier_Settings_ActionVkAt(int i);
GVTCIER_API int* Gvtcier_Settings_ActionVkPtrAt(int i);
