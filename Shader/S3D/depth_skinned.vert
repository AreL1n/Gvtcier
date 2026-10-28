#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in vec4 aJoints;
layout(location = 4) in vec4 aWeights;

#include "gvu.glsl"

void main() {
    mat4 skin = mat4(0.0);
    for (int i = 0; i < 4; ++i)
        skin += uBones[int(aJoints[i])] * aWeights[i];
    vec3 wp = (uModel * (skin * vec4(aPos, 1.0))).xyz - uShadowAnchor;
    gl_Position = uLightVP * vec4(wp, 1.0);
    if (gl_Position.z <= -0.9999) gl_Position.z = -0.9999;
}
