#include <windows.h>
#include "Gvtcier.h"
#include "miniaudio.h"
#include <vector>
#include <string>

// 音频子系统：基于 miniaudio，负责设备初始化、音效播放、流式音乐与 3D 听者。
// 音效资源走 assets/Audio，资源表由构建脚本从目录自动同步生成（audio_gen.gen.h）。
// 解析音频路径。脚本里写 "1_18" 这种 编号_序号 形式的短名时，会去资源表里换成真实文件名；
// 已经带扩展名的普通路径则原样返回。
// 打包模式与裸目录模式取不同的路径：打包模式用资源包内的名字，裸模式用磁盘相对路径。
static bool resolveAudioPath(const char* path, std::string& out) {
    out = path;
    if (!path || !path[0]) return false;
    if (path[0] < '0' || path[0] > '9') return true;
    int i = 0;
    while (path[i] >= '0' && path[i] <= '9') ++i;
    if (i == 0 || path[i] != '_') return true;
    int j = i + 1;
    while (path[j] >= '0' && path[j] <= '9') ++j;
    if (j == i + 1 || path[j] != '\0') return true;
    for (int k = 0; k < kGvAudioSndCount; ++k) {
        if (strcmp(kGvAudioSnds[k].name, path) == 0) {
            if (Gvtcier_Res_HasPack()) {
                out = kGvAudioSnds[k].res;
            } else {
                out = std::string("assets/Audio/") + kGvAudioSnds[k].file;
            }
            return true;
        }
    }
    return false;
}

typedef ALCdevice* (*PFN_alcOpenDevice)(const ALCchar*);
typedef ALCboolean (*PFN_alcCloseDevice)(ALCdevice*);
typedef ALCcontext* (*PFN_alcCreateContext)(ALCdevice*, const ALCint*);
typedef ALCboolean (*PFN_alcMakeContextCurrent)(ALCcontext*);
typedef void (*PFN_alcDestroyContext)(ALCcontext*);
typedef ALenum (*PFN_alGetError)(void);
typedef void (*PFN_alGenSources)(ALsizei, ALuint*);
typedef void (*PFN_alDeleteSources)(ALsizei, const ALuint*);
typedef void (*PFN_alSourcePlay)(ALuint);
typedef void (*PFN_alSourceStop)(ALuint);
typedef void (*PFN_alSourcePause)(ALuint);
typedef void (*PFN_alSourcei)(ALuint, ALenum, ALint);
typedef void (*PFN_alSourcef)(ALuint, ALenum, ALfloat);
typedef void (*PFN_alSource3f)(ALuint, ALenum, ALfloat, ALfloat, ALfloat);
typedef void (*PFN_alGetSourcei)(ALuint, ALenum, ALint*);
typedef void (*PFN_alGetSourcef)(ALuint, ALenum, ALfloat*);
typedef void (*PFN_alGenBuffers)(ALsizei, ALuint*);
typedef void (*PFN_alDeleteBuffers)(ALsizei, const ALuint*);
typedef void (*PFN_alBufferData)(ALuint, ALenum, const void*, ALsizei, ALsizei);
typedef void (*PFN_alListener3f)(ALenum, ALfloat, ALfloat, ALfloat);
typedef void (*PFN_alListenerfv)(ALenum, const ALfloat*);
typedef const ALCchar* (*PFN_alcGetString)(ALCdevice*, ALenum);
typedef void (*PFN_alSourceQueueBuffers)(ALuint, ALsizei, const ALuint*);
typedef void (*PFN_alSourceUnqueueBuffers)(ALuint, ALsizei, ALuint*);

struct AlApi {
    HMODULE lib;
    PFN_alcOpenDevice alcOpenDevice;
    PFN_alcCloseDevice alcCloseDevice;
    PFN_alcCreateContext alcCreateContext;
    PFN_alcMakeContextCurrent alcMakeContextCurrent;
    PFN_alcDestroyContext alcDestroyContext;
    PFN_alcGetString alcGetString;
    PFN_alGetError alGetError;
    PFN_alGenSources alGenSources;
    PFN_alDeleteSources alDeleteSources;
    PFN_alSourcePlay alSourcePlay;
    PFN_alSourceStop alSourceStop;
    PFN_alSourcePause alSourcePause;
    PFN_alSourcei alSourcei;
    PFN_alSourcef alSourcef;
    PFN_alSource3f alSource3f;
    PFN_alGetSourcei alGetSourcei;
    PFN_alGetSourcef alGetSourcef;
    PFN_alGenBuffers alGenBuffers;
    PFN_alDeleteBuffers alDeleteBuffers;
    PFN_alBufferData alBufferData;
    PFN_alListener3f alListener3f;
    PFN_alListenerfv alListenerfv;
    PFN_alSourceQueueBuffers alSourceQueueBuffers;
    PFN_alSourceUnqueueBuffers alSourceUnqueueBuffers;
};

#define GVTS_STREAM_BUFFERS 4
#define GVTS_STREAM_FRAMES 16384

struct GvAlSource {
    ALuint id;
    ALuint buffer;
    float gain;
    float pitch;
    float length;
    bool looping;
    bool spatial;
    int bus;
    bool stream;
    ALenum format;
    ALsizei rate;
    ma_decoder decoder;
    bool decoderOpen;
    bool sourceEnded;
    ALuint streamBufs[GVTS_STREAM_BUFFERS];
    unsigned char* streamData;
    ma_uint64 cursorFrames;
};

static AlApi g_al{};
static ALCdevice* g_device = nullptr;
static ALCcontext* g_context = nullptr;
static float g_busVolume[GVTS_AUDIO_BUS_COUNT] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static bool g_busMute[GVTS_AUDIO_BUS_COUNT] = { false, false, false, false, false };

static int clampBus(int bus) {
    if (bus < 0) return 0;
    if (bus >= GVTS_AUDIO_BUS_COUNT) return GVTS_AUDIO_BUS_COUNT - 1;
    return bus;
}

static float busEffective(int bus) {
    bus = clampBus(bus);
    if (g_busMute[bus]) return 0.0f;
    if (bus == GVTS_AUDIO_BUS_MASTER) return g_busVolume[bus];
    if (g_busMute[GVTS_AUDIO_BUS_MASTER]) return 0.0f;
    return g_busVolume[bus] * g_busVolume[GVTS_AUDIO_BUS_MASTER];
}

static void applySourceGain(GvAlSource* s) {
    if (!s || !g_context) return;
    g_al.alSourcef(s->id, AL_GAIN, s->gain * busEffective(s->bus));
}

static std::vector<GvAlSource*> g_sources;

static void refreshBusSources() {
    for (size_t i = 0; i < g_sources.size(); ++i) applySourceGain(g_sources[i]);
}

static bool loadSym(const char* name, void** out) {
    *out = (void*)GetProcAddress(g_al.lib, name);
    return *out != nullptr;
}

static bool loadAlSym();

static bool loadApi() {
    if (g_al.lib) return true;
    // 第三方运行时统一放在 Gvtcier_Data，先按模块所在目录找，找不到再退回当前目录
    {
        char self[MAX_PATH] = {};
        HMODULE hm = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&loadApi, &hm) && hm) {
            GetModuleFileNameA(hm, self, MAX_PATH);
        }
        std::string dir;
        if (self[0]) {
            std::string p = self;
            size_t sl = p.find_last_of("\\/");
            if (sl != std::string::npos) dir = p.substr(0, sl + 1);
        }
        if (!dir.empty()) {
            // OpenAL32 内部还会按名字去找 wrap_oal.dll，光传全路径不够，
            // 得把这个目录临时加进 DLL 搜索路径，否则它自己的加载会失败
            std::string dataDir = dir + "Gvtcier_Data";
            char oldPath[8192] = {};
            DWORD oldLen = GetEnvironmentVariableA("PATH", oldPath, sizeof(oldPath));
            std::string newPath = dataDir + ";" + (oldLen > 0 ? std::string(oldPath) : std::string());
            SetEnvironmentVariableA("PATH", newPath.c_str());

            std::string candidate = dataDir + "\\OpenAL32.dll";
            g_al.lib = LoadLibraryA(candidate.c_str());

            if (!g_al.lib) {
                SetEnvironmentVariableA("PATH", oldLen > 0 ? oldPath : nullptr);
            }
            if (g_al.lib) return loadAlSym();
        }
    }
    const char* names[] = {"OpenAL32.dll", "openal32.dll", "OpenAL.dll"};
    for (const char* n : names) {
        g_al.lib = LoadLibraryA(n);
        if (g_al.lib) break;
    }
    if (!g_al.lib) return false;
    return loadAlSym();
}

static bool loadAlSym() {
    bool ok = true && loadSym("alcOpenDevice", (void**)&g_al.alcOpenDevice)
                  && loadSym("alcCloseDevice", (void**)&g_al.alcCloseDevice)
                  && loadSym("alcCreateContext", (void**)&g_al.alcCreateContext)
                  && loadSym("alcMakeContextCurrent", (void**)&g_al.alcMakeContextCurrent)
                  && loadSym("alcDestroyContext", (void**)&g_al.alcDestroyContext)
                  && loadSym("alGetError", (void**)&g_al.alGetError)
                  && loadSym("alGenSources", (void**)&g_al.alGenSources)
                  && loadSym("alDeleteSources", (void**)&g_al.alDeleteSources)
                  && loadSym("alSourcePlay", (void**)&g_al.alSourcePlay)
                  && loadSym("alSourceStop", (void**)&g_al.alSourceStop)
                  && loadSym("alSourcePause", (void**)&g_al.alSourcePause)
                  && loadSym("alSourcei", (void**)&g_al.alSourcei)
                  && loadSym("alSourcef", (void**)&g_al.alSourcef)
                  && loadSym("alSource3f", (void**)&g_al.alSource3f)
                  && loadSym("alGetSourcei", (void**)&g_al.alGetSourcei)
                  && loadSym("alGetSourcef", (void**)&g_al.alGetSourcef)
                  && loadSym("alGenBuffers", (void**)&g_al.alGenBuffers)
                  && loadSym("alDeleteBuffers", (void**)&g_al.alDeleteBuffers)
                  && loadSym("alBufferData", (void**)&g_al.alBufferData)
                  && loadSym("alListener3f", (void**)&g_al.alListener3f)
                  && loadSym("alListenerfv", (void**)&g_al.alListenerfv)
                  && loadSym("alSourceQueueBuffers", (void**)&g_al.alSourceQueueBuffers)
                  && loadSym("alSourceUnqueueBuffers", (void**)&g_al.alSourceUnqueueBuffers);
    if (!ok) {
        FreeLibrary(g_al.lib);
        g_al.lib = nullptr;
        return false;
    }
    // 设备名是可选能力，老版本 OpenAL 里没有也不影响播放
    loadSym("alcGetString", (void**)&g_al.alcGetString);
    return true;
}

static bool readAudioBytes(const char* path, std::vector<unsigned char>& out) {
    int sz = 0;
    const void* p = res_get(path, &sz);
    if (p && sz > 0) {
        const unsigned char* b = (const unsigned char*)p;
        out.assign(b, b + sz);
        return true;
    }
    return Gvtcier_Res_ReadFile(path, out) && !out.empty();
}

static bool decodePcm(const char* path, std::vector<short>& pcm, int& channels, int& rate) {
    std::string rpath;
    if (!resolveAudioPath(path, rpath)) return false;
    path = rpath.c_str();
    ma_decoder_config dcfg = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder dec;
    ma_result r = MA_ERROR;
    std::vector<unsigned char> bytes;
    if (readAudioBytes(path, bytes)) {
        r = ma_decoder_init_memory(bytes.data(), bytes.size(), &dcfg, &dec);
    } else {
        r = ma_decoder_init_file(path, &dcfg, &dec);
    }
    if (r != MA_SUCCESS) {
        rlog::Gvtcier_Log_Write(u8"[音频诊断] %s miniaudio 打开失败 code=%d\n", path, (int)r);
        return false;
    }
    ma_uint32 ch = 0, sr = 0;
    ma_decoder_get_data_format(&dec, nullptr, &ch, &sr, nullptr, 0);
    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &total);
    if (total == 0) {
        ma_decoder_uninit(&dec);
        return false;
    }
    pcm.resize((size_t)total * ch);
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&dec, pcm.data(), total, &got);
    ma_decoder_uninit(&dec);
    if (got == 0) return false;
    pcm.resize((size_t)got * ch);
    channels = (int)ch;
    rate = (int)sr;
    return true;
}

static bool openDecoder(const char* path, ma_decoder& dec, std::vector<unsigned char>& hold) {
    std::string rpath;
    if (!resolveAudioPath(path, rpath)) return false;
    path = rpath.c_str();
    ma_decoder_config dcfg = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_result r = MA_ERROR;
    if (readAudioBytes(path, hold)) {
        r = ma_decoder_init_memory(hold.data(), hold.size(), &dcfg, &dec);
    } else {
        r = ma_decoder_init_file(path, &dcfg, &dec);
    }
    return r == MA_SUCCESS;
}

static bool fillStreamBuffer(GvAlSource* s, ALuint buf) {
    unsigned char* out = s->streamData;
    ma_uint64 want = GVTS_STREAM_FRAMES;
    ma_uint64 got = 0;
    ma_result r = ma_decoder_read_pcm_frames(&s->decoder, out, want, &got);
    if (got == 0) {
        if (s->looping) {
            ma_decoder_seek_to_pcm_frame(&s->decoder, 0);
            s->cursorFrames = 0;
            r = ma_decoder_read_pcm_frames(&s->decoder, out, want, &got);
        }
        if (got == 0) { s->sourceEnded = true; return false; }
    }
    s->cursorFrames += got;
    ALsizei bytes = (ALsizei)(got * (ALsizei)(s->format == AL_FORMAT_STEREO16 ? 4 : 2));
    g_al.alBufferData(buf, s->format, out, bytes, s->rate);
    g_al.alSourceQueueBuffers(s->id, 1, &buf);
    return true;
}

bool Gvtcier_Audio_Init() {
    if (g_context) return true;
    if (!loadApi()) return false;
    g_device = g_al.alcOpenDevice(nullptr);
    if (!g_device) return false;
    g_context = g_al.alcCreateContext(g_device, nullptr);
    if (!g_context) {
        g_al.alcCloseDevice(g_device);
        g_device = nullptr;
        return false;
    }
    g_al.alcMakeContextCurrent(g_context);
    g_al.alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    return true;
}

// 能不能把这个资源解出来。开场预载只想知道文件在不在、格式认不认，不需要真的建音源。
bool Gvtcier_Audio_CanLoad(const char* path) {
    if (!path || !path[0]) return false;
    std::string rpath;
    if (!resolveAudioPath(path, rpath)) return false;
    ma_decoder dec;
    std::vector<unsigned char> hold;
    if (!openDecoder(rpath.c_str(), dec, hold)) return false;
    ma_decoder_uninit(&dec);
    return true;
}

// 设备名从 OpenAL 直接问，问不到就空串，调用方自己决定怎么显示
const char* Gvtcier_Audio_DeviceName() {
    if (!g_device) return "";
    static std::string name;
    name.clear();
    if (g_al.alcGetString) {
        const ALCchar* s = g_al.alcGetString(g_device, ALC_DEVICE_SPECIFIER);
        if (s) name = s;
    }
    return name.c_str();
}

void Gvtcier_Audio_Shutdown() {
    g_sources.clear();
    if (g_context) {
        g_al.alcMakeContextCurrent(nullptr);
        g_al.alcDestroyContext(g_context);
        g_context = nullptr;
    }
    if (g_device) {
        g_al.alcCloseDevice(g_device);
        g_device = nullptr;
    }
    if (g_al.lib) {
        FreeLibrary(g_al.lib);
        g_al.lib = nullptr;
    }
}

void Gvtcier_Audio_SetVolume(float v) {
    Gvtcier_Audio_BusSetVolume(GVTS_AUDIO_BUS_MASTER, v);
}

void Gvtcier_Audio_BusSetVolume(int bus, float v) {
    bus = clampBus(bus);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_busVolume[bus] = v;
    refreshBusSources();
}

float Gvtcier_Audio_BusGetVolume(int bus) {
    return g_busVolume[clampBus(bus)];
}

void Gvtcier_Audio_BusSetMute(int bus, bool mute) {
    g_busMute[clampBus(bus)] = mute;
    refreshBusSources();
}

bool Gvtcier_Audio_BusGetMute(int bus) {
    return g_busMute[clampBus(bus)];
}

void* Gvtcier_Audio_CreateSource(const char* path, bool loop) {
    if (!g_context || !path || !path[0]) return nullptr;
    std::vector<short> pcm;
    int channels = 0;
    int rate = 0;
    if (!decodePcm(path, pcm, channels, rate)) return nullptr;
    ALenum fmt = AL_FORMAT_MONO16;
    if (channels >= 2) fmt = AL_FORMAT_STEREO16;
    GvAlSource* s = new GvAlSource();
    s->buffer = 0;
    s->gain = 1.0f;
    s->pitch = 1.0f;
    s->looping = loop;
    s->spatial = false;
    s->bus = GVTS_AUDIO_BUS_SFX;
    s->stream = false;
    s->decoderOpen = false;
    s->sourceEnded = false;
    s->format = AL_FORMAT_MONO16;
    s->rate = rate;
    s->streamData = nullptr;
    s->cursorFrames = 0;
    s->length = (float)pcm.size() / (float)(channels > 0 ? channels : 1) / (float)(rate > 0 ? rate : 48000);
    g_al.alGenBuffers(1, &s->buffer);
    g_al.alBufferData(s->buffer, fmt, pcm.data(), (ALsizei)(pcm.size() * sizeof(short)), rate);
    g_al.alGenSources(1, &s->id);
    g_al.alSourcei(s->id, AL_BUFFER, (ALint)s->buffer);
    g_al.alSourcei(s->id, AL_SOURCE_RELATIVE, AL_TRUE);
    g_al.alSourcef(s->id, AL_GAIN, s->gain * busEffective(s->bus));
    g_al.alSourcef(s->id, AL_PITCH, 1.0f);
    ALint err = g_al.alGetError();
    if (err != AL_NO_ERROR) {
        g_al.alDeleteSources(1, &s->id);
        g_al.alDeleteBuffers(1, &s->buffer);
        delete s;
        return nullptr;
    }
    g_sources.push_back(s);
    return s;
}

void* Gvtcier_Audio_CreateStream(const char* path, bool loop) {
    if (!g_context || !path || !path[0]) return nullptr;
    std::vector<unsigned char> hold;
    GvAlSource* s = new GvAlSource();
    if (!openDecoder(path, s->decoder, hold)) { delete s; return nullptr; }
    ma_uint32 ch = 0, sr = 0;
    ma_decoder_get_data_format(&s->decoder, nullptr, &ch, &sr, nullptr, 0);
    s->decoderOpen = true;
    s->format = (ch >= 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    s->rate = (ALsizei)(sr > 0 ? sr : 48000);
    s->buffer = 0;
    s->gain = 1.0f;
    s->pitch = 1.0f;
    s->looping = loop;
    s->spatial = false;
    s->bus = GVTS_AUDIO_BUS_SFX;
    s->stream = true;
    s->sourceEnded = false;
    s->cursorFrames = 0;
    ma_uint64 total = 0;
    ma_decoder_get_length_in_pcm_frames(&s->decoder, &total);
    s->length = (float)total / (float)(s->rate > 0 ? s->rate : 48000);
    s->streamData = new unsigned char[(size_t)GVTS_STREAM_FRAMES * (ch >= 2 ? 4 : 2)];
    g_al.alGenSources(1, &s->id);
    g_al.alSourcei(s->id, AL_SOURCE_RELATIVE, AL_TRUE);
    g_al.alSourcef(s->id, AL_GAIN, s->gain * busEffective(s->bus));
    g_al.alSourcef(s->id, AL_PITCH, 1.0f);
    g_al.alGenBuffers(GVTS_STREAM_BUFFERS, s->streamBufs);
    for (int i = 0; i < GVTS_STREAM_BUFFERS; ++i) {
        if (!fillStreamBuffer(s, s->streamBufs[i])) break;
    }
    ALint err = g_al.alGetError();
    if (err != AL_NO_ERROR) {
        g_al.alDeleteSources(1, &s->id);
        g_al.alDeleteBuffers(GVTS_STREAM_BUFFERS, s->streamBufs);
        ma_decoder_uninit(&s->decoder);
        delete[] s->streamData;
        delete s;
        return nullptr;
    }
    g_sources.push_back(s);
    return s;
}

void Gvtcier_Audio_StreamTick() {
    if (!g_context) return;
    for (size_t i = 0; i < g_sources.size(); ++i) {
        GvAlSource* s = g_sources[i];
        if (!s->stream) continue;
        ALint processed = 0;
        g_al.alGetSourcei(s->id, AL_BUFFERS_PROCESSED, &processed);
        while (processed > 0) {
            ALuint buf = 0;
            g_al.alSourceUnqueueBuffers(s->id, 1, &buf);
            if (!s->sourceEnded) fillStreamBuffer(s, buf);
            --processed;
        }
        if (s->sourceEnded) {
            ALint queued = 0;
            ALint state = 0;
            g_al.alGetSourcei(s->id, AL_BUFFERS_QUEUED, &queued);
            g_al.alGetSourcei(s->id, AL_SOURCE_STATE, &state);
            if (queued == 0 && state != AL_PLAYING) continue;
        }
    }
}

void Gvtcier_Audio_DestroySource(void* src) {
    if (!src || !g_al.lib) return;
    GvAlSource* s = (GvAlSource*)src;
    bool alive = false;
    for (size_t i = 0; i < g_sources.size(); ++i) {
        if (g_sources[i] == s) { alive = true; g_sources.erase(g_sources.begin() + i); break; }
    }
    if (!alive) return;
    if (g_context && g_al.alSourceStop) g_al.alSourceStop(s->id);
    g_al.alDeleteSources(1, &s->id);
    if (s->stream) {
        g_al.alDeleteBuffers(GVTS_STREAM_BUFFERS, s->streamBufs);
        if (s->decoderOpen) ma_decoder_uninit(&s->decoder);
        delete[] s->streamData;
    } else {
        g_al.alDeleteBuffers(1, &s->buffer);
    }
    delete s;
}

void Gvtcier_Audio_SourcePlay(void* src, bool play) {
    if (!src || !g_context) return;
    GvAlSource* s = (GvAlSource*)src;
    if (play) {
        if (s->stream) {
            ALint queued = 0;
            g_al.alGetSourcei(s->id, AL_BUFFERS_QUEUED, &queued);
            if (queued == 0) {
                s->sourceEnded = false;
                ma_decoder_seek_to_pcm_frame(&s->decoder, 0);
                s->cursorFrames = 0;
                for (int i = 0; i < GVTS_STREAM_BUFFERS; ++i) {
                    if (!fillStreamBuffer(s, s->streamBufs[i])) break;
                }
            }
        } else {
            g_al.alSourcei(s->id, AL_LOOPING, s->looping ? AL_TRUE : AL_FALSE);
        }
        g_al.alSourcePlay(s->id);
    } else {
        g_al.alSourceStop(s->id);
    }
}

void Gvtcier_Audio_SourceSetVolume(void* src, float v) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    s->gain = v;
    applySourceGain(s);
}

void Gvtcier_Audio_SourceSetBus(void* src, int bus) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    s->bus = clampBus(bus);
    applySourceGain(s);
}

int Gvtcier_Audio_SourceGetBus(void* src) {
    if (!src) return GVTS_AUDIO_BUS_SFX;
    return ((GvAlSource*)src)->bus;
}

void Gvtcier_Audio_SourceSetPitch(void* src, float p) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    s->pitch = p;
    g_al.alSourcef(s->id, AL_PITCH, p > 0.01f ? p : 0.01f);
}

void Gvtcier_Audio_SourceSetSpatial(void* src, bool enabled) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    s->spatial = enabled;
    g_al.alSourcei(s->id, AL_SOURCE_RELATIVE, enabled ? AL_FALSE : AL_TRUE);
}

void Gvtcier_Audio_SourceSetAttenuation(void* src, bool enabled, float rolloff) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    if (enabled) {
        g_al.alSourcef(s->id, AL_ROLLOFF_FACTOR, rolloff > 0.01f ? rolloff : 0.01f);
        g_al.alSourcef(s->id, AL_REFERENCE_DISTANCE, 1.0f);
        g_al.alSourcef(s->id, AL_MAX_DISTANCE, 100.0f);
    } else {
        g_al.alSourcef(s->id, AL_ROLLOFF_FACTOR, 0.0f);
    }
}

void Gvtcier_Audio_SourceSetPosition(void* src, const float* pos) {
    if (!src || !pos) return;
    GvAlSource* s = (GvAlSource*)src;
    g_al.alSource3f(s->id, AL_POSITION, pos[0], pos[1], pos[2]);
}

void Gvtcier_Audio_SourceSetPaused(void* src, bool paused) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    ALint state = 0;
    g_al.alGetSourcei(s->id, AL_SOURCE_STATE, &state);
    if (paused) {
        if (state == AL_PLAYING) g_al.alSourcePause(s->id);
    } else if (state == AL_PAUSED) {
        g_al.alSourcePlay(s->id);
    }
}

void Gvtcier_Audio_SourceSeek(void* src, float seconds) {
    if (!src) return;
    GvAlSource* s = (GvAlSource*)src;
    float tgt = seconds > 0.0f ? seconds : 0.0f;
    if (!s->stream) {
        g_al.alSourcef(s->id, AL_SEC_OFFSET, tgt);
        return;
    }
    ALint state = 0;
    g_al.alGetSourcei(s->id, AL_SOURCE_STATE, &state);
    bool wasPlaying = (state == AL_PLAYING);
    g_al.alSourceStop(s->id);
    ALint queued = 0;
    g_al.alGetSourcei(s->id, AL_BUFFERS_QUEUED, &queued);
    while (queued > 0) {
        ALuint buf = 0;
        g_al.alSourceUnqueueBuffers(s->id, 1, &buf);
        --queued;
    }
    s->sourceEnded = false;
    ma_uint64 frame = (ma_uint64)(tgt * (float)s->rate);
    ma_decoder_seek_to_pcm_frame(&s->decoder, frame);
    s->cursorFrames = frame;
    for (int i = 0; i < GVTS_STREAM_BUFFERS; ++i) {
        if (!fillStreamBuffer(s, s->streamBufs[i])) break;
    }
    if (wasPlaying) g_al.alSourcePlay(s->id);
}

float Gvtcier_Audio_SourceCursor(void* src) {
    if (!src) return 0.0f;
    GvAlSource* s = (GvAlSource*)src;
    if (s->stream) return (float)s->cursorFrames / (float)(s->rate > 0 ? s->rate : 48000);
    float off = 0.0f;
    g_al.alGetSourcef(s->id, AL_SEC_OFFSET, &off);
    return off;
}

float Gvtcier_Audio_SourceLength(void* src) {
    if (!src) return 0.0f;
    return ((GvAlSource*)src)->length;
}

void Gvtcier_Audio_SetListener(const float* pos, const float* fwd) {
    if (!g_context) return;
    if (pos) g_al.alListener3f(AL_POSITION, pos[0], pos[1], pos[2]);
    if (fwd) {
        float up[3] = {0.0f, 1.0f, 0.0f};
        float orient[6] = {fwd[0], fwd[1], fwd[2], up[0], up[1], up[2]};
        g_al.alListenerfv(AL_ORIENTATION, orient);
    }
}
