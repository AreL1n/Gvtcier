#version 460 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uDepth;
#include "gvu.glsl"
layout(location = 0) out vec4 fragColor;

void main() {
    float sum = 0.0;
    float weight = 0.0;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / 16.0;
            if (texture(uDepth, uv).r <= 0.00001) continue;
            vec3 c = texture(uScene, uv).rgb;
            float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
            l = min(l, uMaxLum);
            float w = 1.0 / (1.0 + l * l * 0.35);
            sum += log2(max(l, 1e-5)) * w;
            weight += w;
        }
    }
    if (weight < 1e-4) {
        fragColor = vec4(0.18, 0.0, 0.0, 1.0);
        return;
    }
    fragColor = vec4(exp2(sum / weight), 0.0, 0.0, 1.0);
}
