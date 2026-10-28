#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
layout(location = 5) in vec3 aSmoothNormal;

#include "gvu.glsl"

void main() {
    mat4 skin = mat4(0.0);
    for (int i = 0; i < 4; ++i)
        skin += uBones[int(aJoints[i])] * aWeights[i];
    vec4 skinnedPos = skin * vec4(aPos, 1.0);
    vec3 n = mat3(skin) * aSmoothNormal;
    float len = length(n);
    n = len > 1e-6 ? n / len : normalize(mat3(skin) * aNormal);
    vec4 worldPos = uModel * vec4(skinnedPos.xyz + n * uOutlineWidth, 1.0);
    gl_Position = uProj * uView * worldPos;
}
