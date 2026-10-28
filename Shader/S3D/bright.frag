#version 460 core
in vec2 vUV;
uniform sampler2D uScene;
#include "gvu.glsl"
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(uThreshold * 0.6, 0.05);
    float soft = clamp(lum - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float base = max(soft, lum - uThreshold);
    float cap = uThreshold + knee * 2.5;
    float amount = min(base, cap);
    fragColor = vec4(c * (amount / max(lum, 1e-4)), 1.0);
}
