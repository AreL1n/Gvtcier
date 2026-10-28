#pragma once
#include "bicu/core/export.h"

enum GvtcierAudioBus {
    GVTS_AUDIO_BUS_MASTER = 0,
    GVTS_AUDIO_BUS_MUSIC = 1,
    GVTS_AUDIO_BUS_SFX = 2,
    GVTS_AUDIO_BUS_VOICE = 3,
    GVTS_AUDIO_BUS_AMBIENT = 4,
    GVTS_AUDIO_BUS_COUNT = 5
};

GVTCIER_API bool Gvtcier_Audio_Init();
// 当前输出设备的名称，取不到时返回空串。日志里报声卡用。
GVTCIER_API const char* Gvtcier_Audio_DeviceName();
// 探一下这个音频资源能不能解出来，不建音源。开场预载报状态用。
GVTCIER_API bool Gvtcier_Audio_CanLoad(const char* path);
GVTCIER_API void Gvtcier_Audio_Shutdown();
GVTCIER_API void Gvtcier_Audio_SetVolume(float v);

GVTCIER_API void Gvtcier_Audio_BusSetVolume(int bus, float v);
GVTCIER_API float Gvtcier_Audio_BusGetVolume(int bus);
GVTCIER_API void Gvtcier_Audio_BusSetMute(int bus, bool mute);
GVTCIER_API bool Gvtcier_Audio_BusGetMute(int bus);

GVTCIER_API void* Gvtcier_Audio_CreateSource(const char* path, bool loop);
GVTCIER_API void* Gvtcier_Audio_CreateStream(const char* path, bool loop);
GVTCIER_API void Gvtcier_Audio_StreamTick();
GVTCIER_API void Gvtcier_Audio_DestroySource(void* src);
GVTCIER_API void Gvtcier_Audio_SourceSetBus(void* src, int bus);
GVTCIER_API int Gvtcier_Audio_SourceGetBus(void* src);
GVTCIER_API void Gvtcier_Audio_SourcePlay(void* src, bool play);
GVTCIER_API void Gvtcier_Audio_SourceSetVolume(void* src, float v);
GVTCIER_API void Gvtcier_Audio_SourceSetPitch(void* src, float p);
GVTCIER_API void Gvtcier_Audio_SourceSetSpatial(void* src, bool enabled);
GVTCIER_API void Gvtcier_Audio_SourceSetAttenuation(void* src, bool enabled, float rolloff);
GVTCIER_API void Gvtcier_Audio_SourceSetPosition(void* src, const float* pos);
GVTCIER_API void Gvtcier_Audio_SourceSetPaused(void* src, bool paused);
GVTCIER_API void Gvtcier_Audio_SourceSeek(void* src, float seconds);
GVTCIER_API float Gvtcier_Audio_SourceCursor(void* src);
GVTCIER_API float Gvtcier_Audio_SourceLength(void* src);
GVTCIER_API void Gvtcier_Audio_SetListener(const float* pos, const float* fwd);
