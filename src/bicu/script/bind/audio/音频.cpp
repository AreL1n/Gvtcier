#include "Gvtcier.h"

// 脚本绑定：音频。把 gvts 里的音频接口转给 bicu/audio/音频.cpp。
// 音量按总线划分（主 / 音乐 / 音效 / 语音），脚本传中文总线名，这里翻译成枚举编号。

// 把脚本里的中文总线名翻译成枚举。传空或未知名字一律按「音效」处理
static int audioBusByName(const char* name) {
    if (!name) return GVTS_AUDIO_BUS_SFX;
    if (strcmp(name, "主") == 0 || strcmp(name, "主音量") == 0) return GVTS_AUDIO_BUS_MASTER;
    if (strcmp(name, "音乐") == 0) return GVTS_AUDIO_BUS_MUSIC;
    if (strcmp(name, "音效") == 0) return GVTS_AUDIO_BUS_SFX;
    if (strcmp(name, "语音") == 0) return GVTS_AUDIO_BUS_VOICE;
    if (strcmp(name, "环境") == 0) return GVTS_AUDIO_BUS_AMBIENT;
    return GVTS_AUDIO_BUS_SFX;
}

static int audioBusArg(gvts_State* L, int idx) {
    if (gvts_isnumber(L, idx)) return (int)gvts_tointeger(L, idx);
    if (gvts_isstring(L, idx)) return audioBusByName(gvts_tostring(L, idx));
    return GVTS_AUDIO_BUS_SFX;
}

static int g_cr_audioListener(gvts_State* L) {
    float p[3] = { (float)gvtsG_checknumber(L, 1),
                   (float)gvtsG_checknumber(L, 2),
                   (float)gvtsG_checknumber(L, 3) };
    float f[3] = { (float)gvtsG_optnumber(L, 4, 0.0),
                   (float)gvtsG_optnumber(L, 5, 0.0),
                   (float)gvtsG_optnumber(L, 6, -1.0) };
    Gvtcier_Audio_SetListener(p, f);
    return 0;
}

static int g_cr_volume(gvts_State* L) {
    float v = (float)gvtsG_checknumber(L, 1);
    Gvtcier_Audio_SetVolume(v);
    return 0;
}

struct AudioSlot {
    void* src = nullptr;
    std::string path;
    bool loop = false;
    bool playing = false;
};

static std::vector<AudioSlot> g_audioSlots;

static AudioSlot* audioSlot(gvts_State* L, int idx) {
    if (!gvts_isnumber(L, idx)) return nullptr;
    int i = (int)gvts_tointeger(L, idx);
    if (i < 1 || i > (int)g_audioSlots.size()) return nullptr;
    return &g_audioSlots[(size_t)i - 1];
}

static int g_audio_load(gvts_State* L) {
    const char* path = gvtsG_checkstring(L, 1);
    bool loop = gvts_toboolean(L, 2) != 0;
    if (!path || !path[0]) { gvts_pushnil(L); return 1; }
    for (size_t i = 0; i < g_audioSlots.size(); ++i) {
        if (g_audioSlots[i].path == path && g_audioSlots[i].loop == loop) {
            gvts_pushinteger(L, (int)i + 1);
            return 1;
        }
    }
    if (g_audioSlots.size() >= 64) { gvts_pushnil(L); return 1; }
    void* src = Gvtcier_Audio_CreateSource(path, loop);
    if (!src) { gvts_pushnil(L); return 1; }
    AudioSlot s;
    s.src = src;
    s.path = path;
    s.loop = loop;
    g_audioSlots.push_back(s);
    gvts_pushinteger(L, (int)g_audioSlots.size());
    return 1;
}

static int g_audio_loadStream(gvts_State* L) {
    const char* path = gvtsG_checkstring(L, 1);
    bool loop = gvts_toboolean(L, 2) != 0;
    if (!path || !path[0]) { gvts_pushnil(L); return 1; }
    if (g_audioSlots.size() >= 64) { gvts_pushnil(L); return 1; }
    void* src = Gvtcier_Audio_CreateStream(path, loop);
    if (!src) { gvts_pushnil(L); return 1; }
    AudioSlot s;
    s.src = src;
    s.path = path;
    s.loop = loop;
    g_audioSlots.push_back(s);
    gvts_pushinteger(L, (int)g_audioSlots.size());
    return 1;
}

static int g_audio_play(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourcePlay(s->src, true);
    s->playing = true;
    return 0;
}

static int g_audio_stop(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourcePlay(s->src, false);
    s->playing = false;
    return 0;
}

static int g_audio_playing(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    gvts_pushboolean(L, s && s->playing);
    return 1;
}

static int g_audio_volume(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourceSetVolume(s->src, (float)gvtsG_checknumber(L, 2));
    return 0;
}

static int g_audio_pitch(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourceSetPitch(s->src, (float)gvtsG_checknumber(L, 2));
    return 0;
}

static int g_audio_spatial(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourceSetSpatial(s->src, gvts_toboolean(L, 2) != 0);
    return 0;
}

static int g_audio_position(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    float p[3] = { (float)gvtsG_checknumber(L, 2),
                   (float)gvtsG_checknumber(L, 3),
                   (float)gvtsG_checknumber(L, 4) };
    Gvtcier_Audio_SourceSetPosition(s->src, p);
    return 0;
}

static int g_audio_attenuation(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    bool on = gvts_toboolean(L, 2) != 0;
    float rolloff = (float)gvtsG_optnumber(L, 3, 1.0);
    Gvtcier_Audio_SourceSetAttenuation(s->src, on, rolloff);
    return 0;
}

static int g_audio_pause(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourceSetPaused(s->src, gvts_toboolean(L, 2) != 0);
    return 0;
}

static int g_audio_seek(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourceSeek(s->src, (float)gvtsG_checknumber(L, 2));
    return 0;
}

static int g_audio_cursor(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    gvts_pushnumber(L, s ? Gvtcier_Audio_SourceCursor(s->src) : 0.0);
    return 1;
}

static int g_audio_length(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    gvts_pushnumber(L, s ? Gvtcier_Audio_SourceLength(s->src) : 0.0);
    return 1;
}

static int g_audio_destroy(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_DestroySource(s->src);
    s->src = nullptr;
    s->playing = false;
    return 0;
}

void gvtsResetAudioSlots() {
    for (size_t i = 0; i < g_audioSlots.size(); ++i) {
        if (g_audioSlots[i].src) Gvtcier_Audio_DestroySource(g_audioSlots[i].src);
    }
    g_audioSlots.clear();
}

static int g_audio_busVolume(gvts_State* L) {
    int bus = audioBusArg(L, 1);
    Gvtcier_Audio_BusSetVolume(bus, (float)gvtsG_checknumber(L, 2));
    return 0;
}

static int g_audio_busVolumeGet(gvts_State* L) {
    int bus = audioBusArg(L, 1);
    gvts_pushnumber(L, Gvtcier_Audio_BusGetVolume(bus));
    return 1;
}

static int g_audio_busMute(gvts_State* L) {
    int bus = audioBusArg(L, 1);
    Gvtcier_Audio_BusSetMute(bus, gvts_toboolean(L, 2) != 0);
    return 0;
}

static int g_audio_busMuteGet(gvts_State* L) {
    int bus = audioBusArg(L, 1);
    gvts_pushboolean(L, Gvtcier_Audio_BusGetMute(bus) ? 1 : 0);
    return 1;
}

static int g_audio_sourceBus(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    if (!s) return 0;
    Gvtcier_Audio_SourceSetBus(s->src, audioBusArg(L, 2));
    return 0;
}

static int g_audio_sourceBusGet(gvts_State* L) {
    AudioSlot* s = audioSlot(L, 1);
    gvts_pushinteger(L, s ? Gvtcier_Audio_SourceGetBus(s->src) : GVTS_AUDIO_BUS_SFX);
    return 1;
}

void Gvtcier_Bind_Audio(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"音频", u8"设置_全局音量", u8"设置全局音量", g_cr_volume);
    regCap(L, ctx, u8"音频", u8"加载_音效", u8"加载 wav 音效，第二参数为是否循环，返回句柄", g_audio_load);
    regCap(L, ctx, u8"音频", u8"加载_音频流", u8"流式加载长音频（如 BGM），边解码边播放不占内存，第二参数为是否循环，返回句柄", g_audio_loadStream);
    regCap(L, ctx, u8"音频", u8"播放_音效", u8"从头播放指定音效句柄", g_audio_play);
    regCap(L, ctx, u8"音频", u8"停止_音效", u8"停止指定音效句柄", g_audio_stop);
    regCap(L, ctx, u8"音频", u8"检测_音效播放中", u8"查询音效句柄是否正在播放", g_audio_playing);
    regCap(L, ctx, u8"音频", u8"设置_音效音量", u8"设置单个音效的音量", g_audio_volume);
    regCap(L, ctx, u8"音频", u8"设置_音效音高", u8"设置单个音效的播放速度倍率", g_audio_pitch);
    regCap(L, ctx, u8"音频", u8"设置_音效空间化", u8"开关音效的 3D 空间衰减", g_audio_spatial);
    regCap(L, ctx, u8"音频", u8"设置_音效位置", u8"设置空间化音效的世界坐标", g_audio_position);
    regCap(L, ctx, u8"音频", u8"设置_音效衰减", u8"设置空间化音效的衰减与 rolloff", g_audio_attenuation);
    regCap(L, ctx, u8"音频", u8"删除_音效", u8"释放音效句柄占用的资源", g_audio_destroy);
    regCap(L, ctx, u8"音频", u8"设置_音频听者", u8"设置听者的世界坐标与朝向，空间音效用", g_cr_audioListener);
    regCap(L, ctx, u8"音频", u8"设置_音效暂停", u8"暂停或恢复指定音效", g_audio_pause);
    regCap(L, ctx, u8"音频", u8"设置_音效进度", u8"跳转到指定秒数播放位置", g_audio_seek);
    regCap(L, ctx, u8"音频", u8"获取_音效当前秒", u8"读取播放位置秒数", g_audio_cursor);
    regCap(L, ctx, u8"音频", u8"获取_音效时长", u8"读取音效总时长秒数", g_audio_length);
    regCap(L, ctx, u8"音频", u8"设置_音频总线音量", u8"设置总线音量，总线取 主/音乐/音效/语音/环境：设置_音频总线音量(\"音乐\", 值)", g_audio_busVolume);
    regCap(L, ctx, u8"音频", u8"获取_音频总线音量", u8"读取总线音量：获取_音频总线音量(\"音乐\")", g_audio_busVolumeGet);
    regCap(L, ctx, u8"音频", u8"设置_音频总线静音", u8"静音或恢复某总线：设置_音频总线静音(\"音效\", 真)", g_audio_busMute);
    regCap(L, ctx, u8"音频", u8"检测_音频总线静音", u8"查询总线是否静音：检测_音频总线静音(\"音效\")", g_audio_busMuteGet);
    regCap(L, ctx, u8"音频", u8"设置_音效总线", u8"把音效句柄归入某总线，总线取 主/音乐/音效/语音/环境：设置_音效总线(句柄, \"音乐\")", g_audio_sourceBus);
    regCap(L, ctx, u8"音频", u8"获取_音效总线", u8"读取音效句柄所在总线编号：获取_音效总线(句柄)", g_audio_sourceBusGet);
}
