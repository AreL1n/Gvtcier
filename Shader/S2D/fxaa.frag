#version 460 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2 uTexel;
layout(location = 0) out vec4 fragColor;

#define FXAA_SPAN_MAX 8.0
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_REDUCE_MIN (1.0 / 128.0)

void main() {
    vec3 rgbNW = texture(uTex, vUV + vec2(-1.0, -1.0) * uTexel).rgb;
    vec3 rgbNE = texture(uTex, vUV + vec2( 1.0, -1.0) * uTexel).rgb;
    vec3 rgbSW = texture(uTex, vUV + vec2(-1.0,  1.0) * uTexel).rgb;
    vec3 rgbSE = texture(uTex, vUV + vec2( 1.0,  1.0) * uTexel).rgb;
    vec3 rgbM  = texture(uTex, vUV).rgb;
    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lNW = dot(rgbNW, luma);
    float lNE = dot(rgbNE, luma);
    float lSW = dot(rgbSW, luma);
    float lSE = dot(rgbSE, luma);
    float lM = dot(rgbM, luma);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * uTexel;
    vec3 rgbA = 0.5 * (texture(uTex, vUV + dir * (1.0 / 3.0 - 0.5)).rgb + texture(uTex, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uTex, vUV + dir * -0.5).rgb + texture(uTex, vUV + dir * 0.5).rgb);
    float lB = dot(rgbB, luma);
    if (lB < lMin || lB > lMax) fragColor = vec4(rgbA, 1.0);
    else fragColor = vec4(rgbB, 1.0);
}
