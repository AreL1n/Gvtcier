#version 460 core
in vec2 vUV;
uniform sampler2D uDepth;
uniform sampler2D uShadow;
uniform sampler3D uNoise;
#include "gvu.glsl"
uniform int uCloudEnable;
uniform float uCloudAlt;
uniform float uCloudH;
uniform float uCloudDensity;
uniform float uCloudScale;
uniform float uCloudCover;
uniform float uCloudBright;
uniform float uCloudSpeed;
uniform int uCloudSteps;
uniform vec3 uCloudSun;
layout(location=0) out vec4 fragColor;

float hg(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

float shadowVis(vec3 p) {
    vec4 lp = uLightVP * vec4(p, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z < 0.0) return 1.0;
    float s = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            float d = texture(uShadow, q.xy + vec2(float(x), float(y)) * uShadowTexel).r;
            s += (q.z + uShadowBias >= d) ? 1.0 : 0.0;
        }
    return s / 9.0;
}

float densityAt(vec3 p) {
    float d = uDensity;
    d *= exp(-max(p.y + 2.0, 0.0) * 0.055);
    float n = texture(uNoise, p * uNoiseScale).r;
    d *= mix(1.0, n * 2.4, uNoiseAmt);
    return max(d, 0.0);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 cloudHash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float cloudPerlin(vec2 p) {
    vec2 ip = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 ga = cloudHash(ip) * 2.0 - 1.0;
    vec2 gb = cloudHash(ip + vec2(1.0, 0.0)) * 2.0 - 1.0;
    vec2 gc = cloudHash(ip + vec2(0.0, 1.0)) * 2.0 - 1.0;
    vec2 gd = cloudHash(ip + vec2(1.0, 1.0)) * 2.0 - 1.0;
    float a = dot(ga, f);
    float b = dot(gb, f - vec2(1.0, 0.0));
    float c = dot(gc, f - vec2(0.0, 1.0));
    float d = dot(gd, f - vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 1.4;
}

float cloudFbm(vec2 p) {
    float total = 0.0;
    float amp = 1.0;
    float freq = 1.0;
    float maxAmp = 0.0;
    for (int i = 0; i < 6; ++i) {
        float n = cloudPerlin(p * freq + vec2(float(i) * 47.3, float(i) * 83.7)) * 0.5 + 0.5;
        total += n * amp;
        maxAmp += amp;
        amp *= 0.55;
        freq *= 2.2;
    }
    return total / maxAmp;
}

float cloudDensity(vec3 p) {
    vec2 uv = p.xz * uCloudScale;
    float n = cloudFbm(uv);
    float detail = cloudPerlin(uv * 2.33 + vec2(500.0, 300.0)) * 0.5 + 0.5;
    n = n * 0.85 + detail * 0.15;
    float d = clamp((n - uCloudCover) / 0.28, 0.0, 1.0);
    d = d * d * (3.0 - 2.0 * d);
    float h = clamp((p.y - uCloudAlt) / uCloudH, 0.0, 1.0);
    float grad = smoothstep(0.0, 0.22, h) * (1.0 - smoothstep(0.68, 1.0, h));
    return d * grad * 1.5;
}

void main() {
    if (uEnable == 0) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }

    float depth = texture(uDepth, vUV).r;
    bool isSky = depth <= 0.00001;
    vec4 ndc = vec4(vUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 wp = uInvVP * ndc;
    vec3 world = wp.xyz / wp.w;
    vec3 ro = uCamPos;
    vec3 rd = normalize(world - ro);
    float dist = isSky ? 0.0 : min(length(world - ro), uMaxDist);

    vec3 acc = vec3(0.0);
    float trans = 1.0;
    if (!isSky && dist >= 1e-4) {
        float steps = float(uSteps);
        float stepSize = dist / steps;
        float jitter = hash12(gl_FragCoord.xy + vec2(uFrame * 13.7, uFrame * 7.3));
        float t = stepSize * jitter;
        float phase = hg(dot(rd, uSunDir), uPhaseG);
        for (int i = 0; i < uSteps; ++i) {
            vec3 p = ro + rd * t;
            float dens = densityAt(p);
            if (dens > 1e-5) {
                float sigma = dens * uExtinction;
                float stepT = exp(-sigma * stepSize);
                float vis = shadowVis(p);
                acc += trans * (1.0 - stepT) * uSunColor * (vis * phase + uAmbient * 0.25);
                trans *= stepT;
                if (trans < 0.01) break;
            }
            t += stepSize;
            if (t >= dist) break;
        }
    }

    vec3 cAcc = vec3(0.0);
    float cTrans = 1.0;
    if (uCloudEnable == 1 && abs(rd.y) > 1e-4) {
        float tA = (uCloudAlt - ro.y) / rd.y;
        float tB = (uCloudAlt + uCloudH - ro.y) / rd.y;
        if (tA > tB) { float tt = tA; tA = tB; tB = tt; }
        tA = max(tA, 0.0);
        float tFar = tB;
        if (!isSky) tFar = min(tFar, dist);
        if (tB > tA + 1.0 && tFar > tA) {
            float stepLen = (tFar - tA) / float(uCloudSteps);
            float jitter = hash12(gl_FragCoord.xy + vec2(uFrame * 3.1, uFrame * 5.7));
            float t = tA + stepLen * jitter;
            float phase = hg(dot(rd, uSunDir), 0.45);
            for (int i = 0; i < 32; ++i) {
                if (i >= uCloudSteps) break;
                vec3 p = ro + rd * t;
                float dens = cloudDensity(p);
                if (dens > 1e-4) {
                    float tFade = 1.0 / (1.0 + t * 0.0006);
                    float sunThin = 1.0 - 0.85 * pow(max(dot(rd, normalize(uSunDir)), 0.0), 60.0);
                    float sigma = dens * uCloudDensity * tFade * sunThin;
                    float stepT = exp(-sigma * stepLen);
                    float dsun = cloudDensity(p + uSunDir * 14.0);
                    float light = exp(-dsun * 0.45);
                    vec3 li = uCloudSun * uCloudBright * light * (0.3 + phase) + uAmbient * 0.12;
                    cAcc += cTrans * (1.0 - stepT) * li;
                    cTrans *= stepT;
                    if (cTrans < 0.02) break;
                }
                t += stepLen;
                if (t >= tFar) break;
            }
        }
    }

    if (uDebugRed > 0.5) {
        float m = step(0.0015, acc.x);
        float edge = smoothstep(0.0015, 0.004, acc.x) - smoothstep(0.004, 0.02, acc.x);
        fragColor = vec4(vec3(1.0, 0.0, 0.0) * (0.35 * m + 0.65 * edge), 1.0);
        return;
    }
    trans *= cTrans;
    float outTrans = (uMode >= 3) ? trans : cTrans;
    fragColor = vec4(acc + cAcc, outTrans);
}
