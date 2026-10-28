#version 460 core
in vec2 vUV;
uniform sampler2D uSrc;
#include "gvu.glsl"
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 o = uTexel;
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += texture(uSrc, vUV + vec2(-o.x, 0.0)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2( o.x, 0.0)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(0.0, -o.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(0.0,  o.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(-o.x, -o.y)).rgb;
    s += texture(uSrc, vUV + vec2( o.x, -o.y)).rgb;
    s += texture(uSrc, vUV + vec2(-o.x,  o.y)).rgb;
    s += texture(uSrc, vUV + vec2( o.x,  o.y)).rgb;
    fragColor = vec4(s / 16.0 * uIntensity, 1.0);
}
