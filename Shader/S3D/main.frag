#version 460 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform sampler2D uAlbedo;
uniform sampler2DArray uFaceTex;
uniform sampler2DArray uCSM;
uniform sampler2DArray uAsm;
uniform samplerCube uPS0;
uniform samplerCube uPS1;
uniform samplerCube uPS2;
uniform samplerCube uPS3;
#include "gvu.glsl"
#ifndef GV_SHADOW_SINGLE_TAP
#define GV_SHADOW_SINGLE_TAP 0
#endif

float quickHash(vec2 pos) {
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

float asmShadowFactor(vec3 wp, vec3 N) {
    if (uAsmCount == 0) return -1.0;
    vec3 lightDir = normalize(uLightDir);
    for (int i = 0; i < 16; ++i) {
        if (i >= uAsmCount) break;
        vec4 info = uAsmInfo[i];
        vec3 dc = wp - info.xyz;
        if (dot(dc, dc) > info.w * info.w) continue;
        vec4 meta = uAsmMeta[i];
        vec3 sp = dc;
        sp += lightDir * (0.00006 * meta.y);
        vec4 lp = uAsmVP[i] * vec4(sp, 1.0);
        if (lp.w <= 0.0) continue;
        vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
        if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z < 0.0) continue;
        // 3x3 比较计数取平均：反向 Z 下取 max 会把邻域里最靠近光源的那个像素
        // 当成整片遮挡，接触阴影会被压得又黑又硬；取平均才是标准 PCF
        float tap = 0.0;
        float litTaps = 0.0;
        for (int ty = -1; ty <= 1; ++ty) {
            for (int tx = -1; tx <= 1; ++tx) {
                float dt = texture(uAsm, vec3(q.xy + vec2(float(tx), float(ty)) * meta.z, float(i))).r;
                litTaps += (q.z >= dt) ? 1.0 : 0.0;
                tap += 1.0;
            }
        }
        return litTaps / tap;
    }
    return -1.0;
}

float sampleCSM(vec3 wp, vec3 N, float NdotL, float viewZ) {
    int casc = 3;
    if (viewZ < uCSMSplits.x) casc = 0;
    else if (viewZ < uCSMSplits.y) casc = 1;
    else if (viewZ < uCSMSplits.z) casc = 2;
    float asmF = asmShadowFactor(wp, N);
    if (asmF >= 0.0) return asmF;
    float texel = (casc == 0) ? uCSMTexel.x : (casc == 1) ? uCSMTexel.y : (casc == 2) ? uCSMTexel.z : uCSMTexel.w;
    float range = (casc == 0) ? uCSMRange.x : (casc == 1) ? uCSMRange.y : (casc == 2) ? uCSMRange.z : uCSMRange.w;
    vec3 L = normalize(-uLightDir);
    vec3 nrm = normalize(N);
    float NdotLraw = dot(nrm, -L);
    float baseNormalBias = 1.0 - max(0.0, NdotLraw);
    vec3 sp = (wp - uShadowAnchor);
    vec3 normalBias = nrm * (baseNormalBias * 0.488 * texel);
    normalBias -= L * dot(L, normalBias);
    sp += normalBias;
    sp += L * (0.00006 * range);
    vec4 lp = uCSMVP[casc] * vec4(sp, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z < 0.0) return 1.0;
    float dcCenter = texture(uCSM, vec3(q.xy, float(casc))).r;
    float hardShadow = (q.z >= dcCenter) ? 1.0 : 0.0;
    return hardShadow;
}

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

vec3 materialAlbedo(vec2 uv, vec3 base) {
    if (uMaterial == 1) {
        float n = noise(uv * 8.0);
        return vec3(0.42, 0.30, 0.20) * (0.7 + 0.6 * n);
    }
    if (uMaterial == 2) {
        vec2 c = floor(uv * 8.0);
        float ch = mod(c.x + c.y, 2.0);
        return mix(vec3(0.12, 0.12, 0.12), vec3(0.9, 0.9, 0.9), ch);
    }
    if (uMaterial == 3) {
        float n = noise(uv * 10.0);
        return vec3(0.55, 0.52, 0.48) * (0.6 + 0.8 * n);
    }
    if (uMaterial == 4) {
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

float pointShadowFactor(vec3 wp, vec3 sn, vec3 lp, int slot, float radius) {
    vec3 d = wp - lp;
    float dist = length(d);
    if (dist < 1e-4 || dist > radius) return 1.0;
    vec3 L = -d / dist;
    float ndl = max(abs(dot(sn, L)), 0.15);
    float slope = sqrt(max(0.0, 1.0 - ndl * ndl)) / ndl;
    vec3 sp = wp + sn * (0.02 + 0.03 * slope) + L * (0.01 + 0.05 * slope);
    vec3 sd3 = sp - lp;
    dist = length(sd3);
    d = sd3;
    float sd;
    if (slot == 0) sd = texture(uPS0, d).r;
    else if (slot == 1) sd = texture(uPS1, d).r;
    else if (slot == 2) sd = texture(uPS2, d).r;
    else sd = texture(uPS3, d).r;
    if (sd > 1e8) return 1.0;
    return (dist + 0.01 <= sd) ? 1.0 : 0.0;
}

vec4 shadeForward(vec3 albedo, vec3 N, vec3 V) {
    vec3 L = normalize(-uLightDir);
    float NdotLraw = dot(N, L);
    float NdotL = max(NdotLraw, 0.0);
    float shadowF = 1.0;
    if (uCSMOn == 1 && NdotL > 0.0) {
        shadowF = sampleCSM(vWorldPos, N, NdotL, length(vWorldPos - uCamPos));
    }
    vec3 Hraw = L + V;
    vec3 H = length(Hraw) > 1e-3 ? Hraw / length(Hraw) : N;
    float NdotH = max(dot(N, H), 0.0);
    vec3 specular = vec3(0.0);
    if (uSpecular > 0.5) specular = uLightColor * pow(max(NdotH, 1e-4), uShininess) * step(0.0, NdotLraw);

    float skyOccl = 1.0;
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambSky = uAmbientColor;
    vec3 ambGround = uAmbientColor * 0.35;
    vec3 ambient = albedo * uAmbientMul * mix(ambGround, ambSky, hemi);
    ambient *= skyOccl;
    vec3 lit = albedo * uLightColor * NdotL * shadowF + specular * shadowF;

    ivec2 tc = ivec2(gl_FragCoord.xy) / uTileSize;
    int tid = tc.y * uTileW + tc.x;
    uint tileCount = tileData[tid * 2];
    uint tileStart = tileData[tid * 2 + 1];
    if (int(tileCount) > int(uLightLimit)) tileCount = uint(uLightLimit);
    for (uint k = 0u; k < tileCount; ++k) {
        uint li = lightIdx[tileStart + k];
        int base = 1 + int(li) * 13;
        vec3 plPos = vec3(lightData[base], lightData[base + 1], lightData[base + 2]);
        float plRadius = lightData[base + 3];
        vec3 plColor = vec3(lightData[base + 4], lightData[base + 5], lightData[base + 6]);
        float plIntensity = lightData[base + 7];
        float plSlot = lightData[base + 12];
        vec3 PL = plPos - vWorldPos;
        float dist = length(PL);
        PL = PL / max(dist, 1e-4);
        float spotF = 1.0;
        float plCos = lightData[base + 11];
        if (plCos > -0.5) {
            vec3 plDir = vec3(lightData[base + 8], lightData[base + 9], lightData[base + 10]);
            float plDl = dot(-PL, plDir);
            if (plDl < plCos) spotF = 0.0;
            else spotF = clamp((plDl - plCos) / max(1.0 - plCos, 1e-4) * 6.0, 0.0, 1.0);
            spotF = spotF * spotF * (3.0 - 2.0 * spotF);
        }
        float nd = clamp(dist / plRadius, 0.0, 1.0);
        float win = 1.0 - nd;
        win *= win;
        float atten = win * win / (1.0 + dist * dist);
        float pNdotLraw = dot(N, PL);
        float pNdotL = max(pNdotLraw, 0.0);
        vec3 PHraw = PL + V;
        vec3 PH = length(PHraw) > 1e-3 ? PHraw / length(PHraw) : N;
        float pNdotH = max(dot(N, PH), 0.0);
        vec3 pSpec = plColor * pow(max(pNdotH, 1e-4), uShininess) * step(0.0, pNdotLraw);
        float pShade = (plSlot >= 0.0) ? pointShadowFactor(vWorldPos, N, plPos, int(plSlot + 0.5), plRadius) : 1.0;
        lit += atten * spotF * pShade * (albedo * plColor * pNdotL + pSpec) * plIntensity;
    }
    for (int k = 0; k < uDirLightCount; ++k) {
        vec3 DL = normalize(-uDirLights[k].xyz);
        float dNdotL = max(dot(N, DL), 0.0);
        if (dNdotL > 0.0) {
            float dAtten = clamp(uDirLights[k].w, 0.0, 2.0);
            lit += uDirLightCols[k].rgb * albedo * dNdotL * dAtten;
        }
    }
    vec3 color = ambient + lit;
    vec3 toFrag = vWorldPos - uCamPos;
    float dist = length(toFrag);
    float midY = max((uCamPos.y + vWorldPos.y) * 0.5, 0.0);
    float hf = exp(-midY * 0.028);
    float fogAmt = 1.0 - exp(-dist * uFogDensity * hf);
    vec3 rd = toFrag / max(dist, 1e-4);
    float sunAmt = pow(max(dot(rd, L), 0.0), 8.0);
    vec3 fogCol = uFogColor * (0.75 + 0.85 * sunAmt);
    float farEdge = 500.0;
    float farFog = smoothstep(farEdge * 0.85, farEdge, dist);
    float fogMix = max(clamp(fogAmt, 0.0, 0.92), farFog);
    return vec4(mix(color, fogCol, fogMix), 1.0);
}

void main() {
    float nl = length(vNormal);
    vec3 Nw = nl > 1e-10 ? vNormal / nl : vec3(0.0, 1.0, 0.0);
    gNormal = vec4(Nw * 0.5 + 0.5, 1.0);

    if (uSun == 1) {
        fragColor = vec4(2.5, 2.3, 2.0, 1.0);
        gAlbedo = vec4(2.5, 2.3, 2.0, 1.0);
        gMaterial = vec4(0.0, 0.5, float(uMaterial), 0.0);
        return;
    }
    if (uMaterial == 5) {
        fragColor = vec4(2.2, 2.0, 1.6, 1.0);
        gAlbedo = vec4(2.2, 2.0, 1.6, 1.0);
        gMaterial = vec4(0.0, 0.5, float(uMaterial), 0.0);
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
    if (uAlphaTest == 1 && texCol.a < 0.35) discard;
    vec3 albedo = materialAlbedo(vUV, texCol.rgb) * uAlbedoTint;

    vec3 N = Nw;
    vec3 V = normalize(uCamPos - vWorldPos);

    if (uTransparent == 1) {
        vec4 c = shadeForward(albedo, N, V);
        fragColor = vec4(c.rgb, texCol.a * uAlpha);
        gNormal = vec4(0.0);
        gAlbedo = vec4(0.0);
        gMaterial = vec4(0.0);
        return;
    }

    gAlbedo = vec4(albedo, texCol.a);
    gMaterial = vec4(float(uSpecular), 0.5, float(uMaterial), 1.0);
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
