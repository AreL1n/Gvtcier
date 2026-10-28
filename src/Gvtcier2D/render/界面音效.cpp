#include "Gvtcier.h"

namespace {

void* g_src[2] = { nullptr, nullptr };
bool g_ready = false;

const char* g_hoverId = "";
const char* g_pressId = "";

}

namespace GvtUiSound {

void Gvtcier_UiSound_Shutdown() {
    for (int i = 0; i < 2; ++i) {
        if (g_src[i]) {
            Gvtcier_Audio_DestroySource(g_src[i]);
            g_src[i] = nullptr;
        }
    }
    g_ready = false;
}

void Gvtcier_UiSound_Init(const char* hoverId, const char* pressId) {
    Gvtcier_UiSound_Shutdown();
    g_hoverId = hoverId ? hoverId : "";
    g_pressId = pressId ? pressId : "";
    const char* ids[2] = { g_hoverId, g_pressId };
    for (int i = 0; i < 2; ++i) {
        if (!ids[i] || !ids[i][0]) continue;
        g_src[i] = Gvtcier_Audio_CreateSource(ids[i], false);
        if (g_src[i]) {
            Gvtcier_Audio_SourceSetSpatial(g_src[i], false);
            Gvtcier_Audio_SourceSetBus(g_src[i], GVTS_AUDIO_BUS_SFX);
        }
    }
    g_ready = (g_src[UiHover] != nullptr) || (g_src[UiPress] != nullptr);
}

bool Gvtcier_UiSound_Loaded(Kind k) {
    int i = (int)k;
    if (i < 0 || i > 1) return false;
    return g_src[i] != nullptr;
}

const char* Gvtcier_UiSound_Resource(Kind k) {
    int i = (int)k;
    if (i < 0 || i > 1) return "";
    return i == UiHover ? g_hoverId : g_pressId;
}

void Gvtcier_UiSound_Play(Kind k) {
    int i = (int)k;
    if (i < 0 || i > 1 || !g_src[i]) return;
    Gvtcier_Audio_SourcePlay(g_src[i], true);
}

bool Gvtcier_UiSound_Ready() { return g_ready; }

}
