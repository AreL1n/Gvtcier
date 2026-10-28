#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 6) in vec4 aInstM0;
layout(location = 7) in vec4 aInstM1;
layout(location = 8) in vec4 aInstM2;
layout(location = 9) in vec4 aInstM3;
layout(location = 10) in vec4 aInstTint;
layout(location = 11) in vec4 aInstParam;

#include "gvu.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
flat out vec4 vInstTint;
flat out vec4 vInstParam;

void main() {
    mat4 model = mat4(aInstM0, aInstM1, aInstM2, aInstM3);
    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vec3 tn = mat3(model) * aNormal;
    vNormal = length(tn) > 1e-10 ? tn / length(tn) : aNormal;
    vUV = aUV * aInstTint.w;
    vInstTint = aInstTint;
    vInstParam = aInstParam;
    gl_Position = uProj * uView * worldPos;
}
