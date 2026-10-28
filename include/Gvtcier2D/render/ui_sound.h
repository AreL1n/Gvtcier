#pragma once

// 界面音效：悬停与按下两种反馈，主菜单、暂停菜单、设置页共用一套。
// 资源编号来自 assets/Audio/snd，构建时自动扫进音频表。
// 任何一层拿不到音频资源都静默跳过，界面不会因为没声音而失效。
namespace GvtUiSound {

enum Kind {
    UiHover = 0,
    UiPress = 1,
};

// 绑定资源编号并创建音效源。重复调用会先释放旧源。
// 传空串表示该音效不可用。
void Gvtcier_UiSound_Init(const char* hoverId, const char* pressId);

// 释放两个音效源。引擎关闭时调用。
void Gvtcier_UiSound_Shutdown();

// 播放一次。同一种音效连着触发时从头重放，不做去重。
void Gvtcier_UiSound_Play(Kind k);

// 是否已成功绑定（用于界面判断要不要显示声音开关）。
bool Gvtcier_UiSound_Ready();

// 某个音效是不是真的装着，开场自检报状态用。
bool Gvtcier_UiSound_Loaded(Kind k);

// 某个音效绑的资源编号，自检打日志时拼名字用。
const char* Gvtcier_UiSound_Resource(Kind k);

}
