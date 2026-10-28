#version 460 core
in vec2 vUV;
uniform sampler2D uTex;
#include "gvu.glsl"
layout(location=0) out vec4 fragColor;
void main() {
    float w[5] = float[5](0.227027, 0.194594, 0.121621, 0.054054, 0.016216);
    vec3 c = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uTex, vUV + uDir * float(i)).rgb * w[i];
        c += texture(uTex, vUV - uDir * float(i)).rgb * w[i];
    }
    fragColor = vec4(c, 1.0);
}
