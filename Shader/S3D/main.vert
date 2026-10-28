#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

#include "gvu.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vec3 tn = mat3(uModel) * aNormal;
    vNormal = length(tn) > 1e-10 ? tn / length(tn) : aNormal;
    vUV = aUV * uUvScale;
    gl_Position = uProj * uView * worldPos;
}
