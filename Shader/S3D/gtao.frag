#version 460 core
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uNormalTex;
#include "gvu.glsl"
layout(location=0) out vec4 fragColor;
layout(location=1) out vec4 outEdges;

vec3 viewPosFromDepth(vec2 uv, float d) {
    vec4 clip = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 v = uInvProj * clip;
    return v.xyz / v.w;
}

float viewZFromDepth(float d) {
    float ndcZ = d * 2.0 - 1.0;
    return uInvProj[3][2] / (ndcZ * uInvProj[2][3] + uInvProj[3][3]);
}

vec4 calcEdges(vec2 uv, float centerZ) {
    vec2 texel = 1.0 / uRes;
    vec2 uvL = uv - vec2(texel.x, 0.0);
    vec2 uvR = uv + vec2(texel.x, 0.0);
    vec2 uvT = uv - vec2(0.0, texel.y);
    vec2 uvB = uv + vec2(0.0, texel.y);
    float zL = viewZFromDepth(texture(uDepth, uvL).r);
    float zR = viewZFromDepth(texture(uDepth, uvR).r);
    float zT = viewZFromDepth(texture(uDepth, uvT).r);
    float zB = viewZFromDepth(texture(uDepth, uvB).r);
    vec4 e = vec4(zL, zR, zT, zB) - centerZ;
    float slopeLR = (e.y - e.x) * 0.5;
    float slopeTB = (e.w - e.z) * 0.5;
    vec4 adjusted = e + vec4(slopeLR, -slopeLR, slopeTB, -slopeTB);
    e = min(abs(e), abs(adjusted));
    return clamp(vec4(1.25) - e / (abs(centerZ) * 0.011), 0.0, 1.0);
}

float packEdges(vec4 e) {
    e = round(clamp(e, 0.0, 1.0) * 2.9);
    return dot(e, vec4(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float horizonSearch(vec2 uv, vec2 uvScale, vec3 P, vec3 N, vec2 dirUV) {
    float maxSin = 0.0;
    vec2 dirStep = vec2(dirUV.x * uvScale.x, dirUV.y * uvScale.y);
    vec2 limit = vec2(1e9);
    if (dirStep.x > 1e-9) limit.x = (1.0 - uv.x) / dirStep.x;
    else if (dirStep.x < -1e-9) limit.x = uv.x / -dirStep.x;
    if (dirStep.y > 1e-9) limit.y = (1.0 - uv.y) / dirStep.y;
    else if (dirStep.y < -1e-9) limit.y = uv.y / -dirStep.y;
    float maxLen = min(limit.x, limit.y);
    if (maxLen <= 0.0) return 0.0;
    float radius = min(uRadius, maxLen);

    for (int i = 1; i <= uSteps; ++i) {
        float t = (float(i) + 0.5) / float(uSteps);
        float len = radius * t * t;
        vec2 suv = uv + dirStep * len;

        float sd = texture(uDepth, suv).r;
        if (sd <= 0.0) continue;

        vec3 S = viewPosFromDepth(suv, sd);
        vec3 D = S - P;
        float distSq = dot(D, D);
        if (distSq < 1e-8) continue;
        float dist = sqrt(distSq);
        D /= dist;

        float sinH = dot(D, N) - uBias;

        float falloff = clamp((radius - dist) / max(uFalloffRange, 1e-3), 0.0, 1.0);
        sinH = mix(0.0, sinH, falloff);

        if (sinH > maxSin) maxSin = sinH;
    }
    return asin(clamp(maxSin, -1.0, 1.0));
}

float arcPrimitive(float u, float n) {
    u = clamp(u, -1.0, 1.0);
    float s = sqrt(max(1.0 - n * n * u * u, 0.0));
    if (n < 1e-5) return u;
    return 0.5 * (u * s + asin(clamp(n * u, -1.0, 1.0)) / n);
}

float integrateArc(float upperOccl, float lowerOccl, float n) {
    float t1 = -1.57079633 + lowerOccl;
    float t2 = 1.57079633 - upperOccl;
    if (t2 <= t1) return 0.0;
    return arcPrimitive(sin(t2), n) - arcPrimitive(sin(t1), n);
}

void main() {
    float d = texture(uDepth, vUV).r;

    if (d <= 0.0) {
        fragColor = vec4(1.0);
        outEdges = vec4(0.0);
        return;
    }
    float centerZ = viewPosFromDepth(vUV, d).z;
    outEdges = vec4(packEdges(calcEdges(vUV, centerZ)));

    vec3 P = viewPosFromDepth(vUV, d);
    vec3 Nw = normalize(texture(uNormalTex, vUV).xyz * 2.0 - 1.0);
    vec3 N = normalize((uView * vec4(Nw, 0.0)).xyz);

    vec3 V = normalize(-P);
    float n = dot(N, V);
    if (n < 0.05) { fragColor = vec4(1.0); return; }

    vec2 uvScale = vec2(uProj[0][0], uProj[1][1]) * 0.5 / max(-P.z, 1e-3);

    float jitter = ign(gl_FragCoord.xy);
    float sliceRot = jitter;

    float visibilitySum = 0.0;
    float unoccluded = integrateArc(0.0, 0.0, n);

    for (int s = 0; s < uSlices; ++s) {
        float phi = (float(s) + sliceRot) * (3.14159265 / float(uSlices));
        vec2 dirUV = vec2(cos(phi), sin(phi));

        float occUpper = horizonSearch(vUV, uvScale, P, N, dirUV);
        float occLower = horizonSearch(vUV, uvScale, P, N, -dirUV);

        visibilitySum += integrateArc(occUpper, occLower, n);
    }

    float visibility = visibilitySum / max(float(uSlices) * unoccluded, 1e-5);
    visibility = clamp(visibility, 0.0, 1.0);
    float occlusion = 1.0 - visibility;

    occlusion *= smoothstep(0.0, 0.35, n);

    occlusion = clamp(occlusion * uIntensity, 0.0, 1.0);
    float ao = pow(1.0 - occlusion, uPower);

    fragColor = vec4(ao, ao, ao, 1.0);
}
