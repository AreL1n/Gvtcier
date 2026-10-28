#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;
#include "gvu.glsl"
out vec3 vNormal;
out vec2 vUv;
out vec3 vWorldPos;
void main() {
    mat4 skin = mat4(0.0);
    for (int i = 0; i < 4; ++i)
        skin += uBones[int(aJoints[i])] * aWeights[i];
    vec4 wpos = skin * vec4(aPos, 1.0);
    vWorldPos = wpos.xyz;
    vec3 tn = mat3(skin) * aNormal;
    vNormal = length(tn) > 1e-10 ? tn / length(tn) : aNormal;
    vUv = aUv;
    gl_Position = uProj * uView * wpos;
}
