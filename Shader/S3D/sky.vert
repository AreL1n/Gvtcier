#version 460 core
layout(location = 0) in vec3 aPos;
#include "gvu.glsl"
out vec2 vScreenPos;

void main() {
    vec3 dome = vec3(aPos.x, aPos.y * 0.40, aPos.z);
    vec4 clip = uProj * uView * vec4(uCamPos + dome, 1.0);
    vScreenPos = clip.xy / clip.w;
    gl_Position = clip;
}
