#version 460 core
layout(location = 0) in vec3 aPos;

#include "gvu.glsl"

out vec3 vRel;

void main() {
    // 视矩阵必须建在光源原点相对空间——这里先减 anchor 再乘 uLightVP。
    // 曾经两边各减一次 anchor，几何整体甩出视锥，手电筒白渲一整晚。
    vec3 wp = (uModel * vec4(aPos, 1.0)).xyz - uShadowAnchor;
    vRel = wp;
    gl_Position = uLightVP * vec4(wp, 1.0);
}
