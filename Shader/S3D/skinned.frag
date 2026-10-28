#version 460 core
in vec3 vNormal;
in vec2 vUv;
in vec3 vWorldPos;
uniform sampler2D uTex;
#include "gvu.glsl"
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gMaterial;
void main() {
    float nl = length(vNormal);
    vec3 n = nl > 1e-10 ? vNormal / nl : vec3(0.0, 1.0, 0.0);
    vec4 tex = texture(uTex, vUv);
    vec3 albedo = pow(tex.rgb, vec3(2.2)) * uBaseColor.rgb;

    gNormal = vec4(n * 0.5 + 0.5, 1.0);
    gAlbedo = vec4(albedo, tex.a);
    gMaterial = vec4(1.0, 0.5, 0.0, 1.0);
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
