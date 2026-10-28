#version 460 core
in vec2 vScreenPos;
#include "gvu.glsl"
uniform sampler2D uGradLut;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gMaterial;

void main() {
    vec4 fp = uInvVP * vec4(vScreenPos, 1.0, 1.0);
    vec3 rd = normalize(fp.xyz / fp.w - uCamPos);
    float h = clamp(rd.y, -1.0, 1.0);
    vec3 col = texture(uGradLut, vec2(h * 0.5 + 0.5, 0.5)).rgb;

    vec3 sd = normalize(uSunDir);
    float ca = clamp(dot(rd, sd), -1.0, 1.0);
    float ang = acos(ca);

    // 真实太阳的角半径是 0.2666 度，即 0.00465 弧度。
    // 硬边圆盘 + 收紧过的两层光晕。
    const float kSunAngRadius = 0.00465;
    float disk = 1.0 - smoothstep(kSunAngRadius * 0.9, kSunAngRadius, ang);
    float glow = exp(-ang * ang * 900.0) * 0.9;
    float halo = exp(-ang * ang * 140.0) * 0.28;

    // 圆盘给足强度让它在色调映射后烧成纯白，这才是太阳该有的样子
    col += uSunColor * (0.10 + disk * 16.0 + glow + halo) * uSunOnScreen;

    fragColor = vec4(col, 1.0);
    gNormal = vec4(0.0);
    gAlbedo = vec4(col, 1.0);
    gMaterial = vec4(0.0, 0.0, 0.0, 0.0);
}
