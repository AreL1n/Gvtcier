#include "Gvtcier.h"

// 安卓平台的绑定占位：桌面端已有的某些脚本接口在安卓上不可用（例如某些音频能力），
// 这里注册成返回 nil 的空实现，保证脚本调用不会报"函数不存在"。

static int g_android_audioUnavailable(gvts_State* L) {
    gvts_pushnil(L);
    return 1;
}

static int g_android_audioNoop(gvts_State* L) {
    return 0;
}

static int g_android_netUnavailable(gvts_State* L) {
    gvts_pushnil(L);
    return 1;
}

static int g_android_netNoop(gvts_State* L) {
    return 0;
}

void Gvtcier_Bind_Audio(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"音频", u8"设置_全局音量", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"加载_音效", u8"安卓端不可用，返回 空", g_android_audioUnavailable);
    regCap(L, ctx, u8"音频", u8"加载_音频流", u8"安卓端不可用，返回 空", g_android_audioUnavailable);
    regCap(L, ctx, u8"音频", u8"播放_音效", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"停止_音效", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"检测_音效播放中", u8"安卓端不可用，恒为 假", g_android_audioUnavailable);
    regCap(L, ctx, u8"音频", u8"设置_音效音量", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音效音高", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音效空间化", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音效位置", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音效衰减", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"删除_音效", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音频听者", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音效暂停", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音效进度", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"获取_音效当前秒", u8"安卓端不可用，返回 0", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"获取_音效时长", u8"安卓端不可用，返回 0", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音频总线音量", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"获取_音频总线音量", u8"安卓端不可用，返回 0", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"设置_音频总线静音", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"检测_音频总线静音", u8"安卓端不可用，恒为 假", g_android_audioUnavailable);
    regCap(L, ctx, u8"音频", u8"设置_音效总线", u8"安卓端不可用，无操作", g_android_audioNoop);
    regCap(L, ctx, u8"音频", u8"获取_音效总线", u8"安卓端不可用，返回 0", g_android_audioNoop);
}

void Gvtcier_Bind_Net(gvts_State* L, GvtsEngineCtx* ctx) {
    regCap(L, ctx, u8"网络", u8"连接_服务器", u8"安卓端不可用，返回 空", g_android_netUnavailable);
    regCap(L, ctx, u8"网络", u8"监听_端口", u8"安卓端不可用，返回 空", g_android_netUnavailable);
    regCap(L, ctx, u8"网络", u8"接受_连接", u8"安卓端不可用，返回 空", g_android_netUnavailable);
    regCap(L, ctx, u8"网络", u8"发送_文本", u8"安卓端不可用，返回 假", g_android_netUnavailable);
    regCap(L, ctx, u8"网络", u8"接收_文本", u8"安卓端不可用，返回 空", g_android_netUnavailable);
    regCap(L, ctx, u8"网络", u8"断开_连接", u8"安卓端不可用，无操作", g_android_netNoop);
}

void gvtsResetAudioSlots() {
}
