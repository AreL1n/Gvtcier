#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 5) in vec3 aSmoothNormal;

#include "gvu.glsl"

void main() {
    vec3 n = aSmoothNormal;
    float len = length(n);
    n = len > 1e-6 ? n / len : normalize(aNormal);
    vec4 worldPos = uModel * vec4(aPos + n * uOutlineWidth, 1.0);
    gl_Position = uProj * uView * worldPos;
}
