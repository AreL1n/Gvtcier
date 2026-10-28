#pragma once
#include "bicu/core/export.h"

// 抬头显示需要的外部数据，由调用方每帧填好传进来。
// fps/ms 由 Hud 自己统计，所以这里不包含；elapsed 是游戏运行秒数，px/py/pz 是玩家世界坐标。
struct HudStats {
    float fps = 0.0f;
    float ms = 0.0f;
    float elapsed = 0.0f;
    float px = 0.0f, py = 0.0f, pz = 0.0f;
};

class Renderer;

// 抬头显示（调试信息面板 + 准星）。
class GVTCIER_API Hud {
public:
    // 每帧喂一次帧间隔，内部做 0.5 秒滑动窗口平均，非法值会被丢弃。
    void Gvtcier_Hud_Update(float frameDt);
    // 绘制面板与准星。showPanel/showCrosshair 分别控制两者是否出现。
    void Gvtcier_Hud_Draw(Renderer& renderer, int w, int h, const HudStats& st,
                          bool showPanel, bool showCrosshair);

    float fps() const { return fps_; }
    float ms() const { return ms_; }

private:
    float fps_ = 0.0f;     // 最近一个统计窗口的平均帧率
    float ms_ = 0.0f;      // 最近一个统计窗口的平均帧时间（毫秒）
    float accum_ = 0.0f;   // 当前窗口已累计的秒数
    int frames_ = 0;       // 当前窗口已累计的帧数
};
