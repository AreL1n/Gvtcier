#version 460 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
flat in vec4 vInstTint;
flat in vec4 vInstParam;

uniform sampler2D uAlbedo;
uniform sampler2DArray uFaceTex;
#include "gvu.glsl"

layout(std430, binding = 0) readonly buffer Lights { float lightData[]; };
layout(std430, binding = 1) readonly buffer Tiles { uint tileData[]; };
layout(std430, binding = 2) readonly buffer Indices { uint lightIdx[]; };

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedo;
layout(location = 3) out vec4 gMaterial;

const float PI = 3.14159265;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 materialAlbedo(vec2 uv, vec3 base, int mat) {
    if (mat == 1) {
        float n = noise(uv * 8.0);
        return vec3(0.42, 0.30, 0.20) * (0.7 + 0.6 * n);
    }
    if (mat == 2) {
        vec2 c = floor(uv * 8.0);
        float ch = mod(c.x + c.y, 2.0);
        return mix(vec3(0.12, 0.12, 0.12), vec3(0.9, 0.9, 0.9), ch);
    }
    if (mat == 3) {
        float n = noise(uv * 10.0);
        return vec3(0.55, 0.52, 0.48) * (0.6 + 0.8 * n);
    }
    if (mat == 4) {
        vec2 p = uv * vec2(6.0, 3.0);
        vec2 ip = floor(p);
        vec2 f = fract(p);
        if (mod(ip.y, 2.0) == 1.0) ip.x += 0.5;
        float mortar = step(0.92, max(f.x, f.y));
        vec3 col = mix(vec3(0.62, 0.30, 0.20), vec3(0.75, 0.72, 0.68), mortar);
        float n = noise(uv * 12.0);
        return col * (0.8 + 0.4 * n);
    }
    return base;
}

void main() {
    int instMaterial = int(vInstParam.x + 0.5);
    int instAlphaTest = int(vInstParam.y + 0.5);
    vec3 instTint = vInstTint.rgb;

    float nl = length(vNormal);
    vec3 Nw = nl > 1e-10 ? vNormal / nl : vec3(0.0, 1.0, 0.0);
    gNormal = vec4(Nw * 0.5 + 0.5, 1.0);

    if (uSun == 1) {
        fragColor = vec4(2.5, 2.3, 2.0, 1.0);
        gAlbedo = vec4(2.5, 2.3, 2.0, 1.0);
        gMaterial = vec4(0.0, 0.5, float(instMaterial), 0.0);
        return;
    }
    if (instMaterial == 5) {
        fragColor = vec4(2.2, 2.0, 1.6, 1.0);
        gAlbedo = vec4(2.2, 2.0, 1.6, 1.0);
        gMaterial = vec4(0.0, 0.5, float(instMaterial), 0.0);
        return;
    }

    vec4 texCol = texture(uAlbedo, vUV);
    if (uFaceTexOn == 1) {
        vec3 an = abs(Nw);
        float layer = 4.0;
        if (an.x >= an.y && an.x >= an.z) layer = (Nw.x > 0.0) ? 0.0 : 1.0;
        else if (an.y >= an.x && an.y >= an.z) layer = (Nw.y > 0.0) ? 2.0 : 3.0;
        else layer = (Nw.z > 0.0) ? 4.0 : 5.0;
        texCol = texture(uFaceTex, vec3(vUV, layer));
    }
    if (instAlphaTest == 1 && texCol.a < 0.35) discard;
    vec3 albedo = materialAlbedo(vUV, texCol.rgb, instMaterial) * instTint;

    gAlbedo = vec4(albedo, texCol.a);
    gMaterial = vec4(1.0, 0.5, float(instMaterial), 1.0);
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
