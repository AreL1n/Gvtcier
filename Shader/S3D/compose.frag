#version 460 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uAO;
uniform sampler2D uVolume;
uniform sampler2D uDepthTex;
#include "gvu.glsl"
layout(location = 0) out vec4 fragColor;

vec3 aces(vec3 color) {
    const float exposure_bias = 1.6f;
    const float A = 0.0245786f;
    const float B = 0.000090537f;
    const float C = 0.983729f;
    const float D = 0.432951f;
    const float E = 0.238081f;
    const mat3 rgb_to_rrt = mat3(
        vec3(0.59719f * exposure_bias, 0.35458f * exposure_bias, 0.04823f * exposure_bias),
        vec3(0.07600f * exposure_bias, 0.90834f * exposure_bias, 0.01566f * exposure_bias),
        vec3(0.02840f * exposure_bias, 0.13383f * exposure_bias, 0.83777f * exposure_bias));
    const mat3 odt_to_rgb = mat3(
        vec3(1.60475f, -0.53108f, -0.07367f),
        vec3(-0.10208f, 1.10813f, -0.00605f),
        vec3(-0.00327f, -0.07276f, 1.07602f));
    color *= rgb_to_rrt;
    color = (color * (color + A) - B) / (color * (C * color + D) + E);
    color *= odt_to_rgb;
    return color;
}

void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    float ao = mix(1.0, texture(uAO, vUV).r, uAOOn);
    scene *= mix(1.0, ao, uAOFade);
    vec4 vol = texture(uVolume, vUV);
    vec3 bloom = texture(uBloom, vUV).rgb;

    float skyD = texture(uDepthTex, vUV).r;
    vec3 color;
    if (skyD <= 0.00001) {
        color = scene * mix(1.0, vol.a, uSunStrength) + vol.rgb * uSunStrength + bloom * uBloomStrength;
    } else {
        float shadeA = texture(uScene, vUV).a;
        float expo = mix(uExposure, 1.0, 1.0 - shadeA);
        vec3 hdr = scene * expo * mix(1.0, vol.a, uSunStrength);
        hdr += vol.rgb * uSunStrength;
        color = aces(hdr + bloom * uBloomStrength);
        color = clamp((color - 0.5) * uContrast + 0.5, 0.0, 1.0);
        float lum = dot(color, vec3(0.299, 0.587, 0.114));
        color = mix(vec3(lum), color, 1.05);
    }

    vec2 vc = vUV - 0.5;
    float vig = 1.0 - uVignette * dot(vc, vc) * 2.0;
    color *= clamp(vig, 0.0, 1.0);

    fragColor = vec4(color, 1.0);
}
