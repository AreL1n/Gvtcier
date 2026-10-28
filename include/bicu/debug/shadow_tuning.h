#pragma once

// 阴影调参弹窗的编译开关：置 1 后引擎启动时会额外开一个「阴影调试」小窗，
// 可以实时拖 8 个偏移/滤波参数、切换单采样、texel 网格与级联层可视化。
// 调参完成后置 0 可彻底关掉（不创建线程、不占屏）。
#define GVTCIER_CSM_TUNING 0

struct ShadowTuning {
    float lightBias;
    float normalBias;
    float slopeClamp;
    float diskRadius;
    float pcfRadius;
    float zPad;
    float splitLambda;
    float biasScale;
    float blendBand;
    int debug;
    int debugLayer;
    int singleTap;
    int showTexelGrid;
    int showWorldTexel;
};

struct ShadowProbe {
    float valid;
    float casc;
    float qz;
    float dc;
    float diff;
    float texelWorld;
    float range;
    float slopeTan;
    float normalBiasAmt;
    float depthBiasAmt;
};

ShadowTuning& Gvtcier_Shadow_Tuning();
ShadowProbe& Gvtcier_Shadow_Probe();
int* Gvtcier_Shadow_ProbeCursor();
void Gvtcier_Shadow_Tuning_Init();
void Gvtcier_Shadow_Tuning_RefreshProbe();
