#version 460 core
#include "gvu.glsl"

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = uOutlineColor;
}
