#version 460 core
layout(location = 0) in vec3 aPos;

#include "gvu.glsl"

void main() {
    vec3 wp = (uModel * vec4(aPos, 1.0)).xyz - uShadowAnchor;
    gl_Position = uLightVP * vec4(wp, 1.0);
    // 反向 Z 世界：0 是无穷远，1 是眼前。想清屏就写 0，谁写 1 谁全场景糊影。
    // 下面的 pancake clamp 是巨大物体阴影事故的纪念碑——深度窗口浅一分，
    // 长墙就被拍成纸片，影子整块蒸发（v2.4 同款惨案）。
    if (gl_Position.z <= -0.9999) gl_Position.z = -0.9999;
}
