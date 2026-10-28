#version 460 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBlur;
uniform vec2 uRes;
uniform vec4 uPanel;
uniform float uAlpha;
uniform float uRound;
uniform float uTint;
layout(location = 0) out vec4 fragColor;

float roundMask(vec2 uv, vec4 p, float r) {
    vec2 halfSize = p.zw * 0.5;
    vec2 c = p.xy + halfSize;
    vec2 d = abs(uv * uRes - c) - (halfSize - vec2(r));
    float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
    return 1.0 - smoothstep(-1.0, 1.0, dist);
}

void main() {
    vec3 sharp = texture(uScene, vUV).rgb;
    vec3 blurred = texture(uBlur, vUV).rgb;
    vec3 frost = blurred * 0.94 + vec3(0.5, 0.55, 0.65) * uTint;
    float m = roundMask(vUV, uPanel, uRound);
    float border = smoothstep(0.0, 1.5, m) - smoothstep(1.5, 3.0, m);
    vec3 outc = mix(sharp, frost, uAlpha * m);
    outc += vec3(1.0) * border * 0.10 * m;
    fragColor = vec4(outc, 1.0);
}
