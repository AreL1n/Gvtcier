#version 460 core
in vec2 vUV;

uniform sampler2D uGNormal;
uniform sampler2D uGAlbedo;
uniform sampler2D uGMaterial;
uniform sampler2D uDepth;
#include "gvu.glsl"

layout(std430, binding = 0) readonly buffer Lights { float lightData[]; };
layout(std430, binding = 1) readonly buffer Tiles { uint tileData[]; };
layout(std430, binding = 2) readonly buffer Indices { uint lightIdx[]; };

uniform sampler2DArray uCSM;
uniform sampler2DArray uAsm;
uniform samplerCube uPS0;
uniform samplerCube uPS1;
uniform samplerCube uPS2;
uniform samplerCube uPS3;
uniform sampler2D uAO;
#ifndef GV_SHADOW_SINGLE_TAP
#define GV_SHADOW_SINGLE_TAP 0
#endif
float gDebug = 0.0;
float gCascId = 0.0;
float gCascOut = 0.0;
float gSlopeTan = 0.0;
float gTexelWorld = 0.0;
float gRange = 0.0;
float gNormalBiasAmt = 0.0;
float gDepthBiasAmt = 0.0;
vec3 gQ = vec3(0.0);
float gD = 0.0;
float gDiff = 0.0;

float quickHash(vec2 pos) {
    const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(pos, magic.xy)));
}

float cheapShadow(vec3 wp, vec3 sn) {
    float viewZ = length(wp - uCamPos);
    int casc = 3;
    if (viewZ < uCSMSplits.x) casc = 0;
    else if (viewZ < uCSMSplits.y) casc = 1;
    else if (viewZ < uCSMSplits.z) casc = 2;
    float texel = (casc == 0) ? uCSMTexel.x : (casc == 1) ? uCSMTexel.y : (casc == 2) ? uCSMTexel.z : uCSMTexel.w;
    float range = (casc == 0) ? uCSMRange.x : (casc == 1) ? uCSMRange.y : (casc == 2) ? uCSMRange.z : uCSMRange.w;
    vec3 lightDir = normalize(uLightDir);
    float NdotLraw = dot(sn, -lightDir);
    float baseNormalBias = 1.0 - max(0.0, NdotLraw);
    float nl = max(abs(NdotLraw), 0.15);
    float slopeTan = sqrt(max(0.0, 1.0 - nl * nl)) / nl;
    vec3 sp = wp - uShadowAnchor;
    vec3 normalBias = sn * (baseNormalBias * uCSMTune.y * texel);
    normalBias -= lightDir * dot(lightDir, normalBias);
    sp += normalBias;
    sp += lightDir * (uCSMTune.x * range + texel * slopeTan * uCSMTune.z + uCSMTune2.y);
    vec4 lp = uCSMVP[casc] * vec4(sp, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z < 0.0) return 1.0;
    float d = texture(uCSM, vec3(q.xy, float(casc))).r;
    return (q.z >= d) ? 1.0 : 0.0;
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

float sampleCSM(vec3 wp, vec3 N, float viewZ) {
    int casc = 3;
    if (viewZ < uCSMSplits.x) casc = 0;
    else if (viewZ < uCSMSplits.y) casc = 1;
    else if (viewZ < uCSMSplits.z) casc = 2;
    gCascId = float(casc);
    float asmF = asmShadowFactor(wp, N);
    if (asmF >= 0.0) {
        gCascId = -1.0;
        return asmF;
    }
    float texel = (casc == 0) ? uCSMTexel.x : (casc == 1) ? uCSMTexel.y : (casc == 2) ? uCSMTexel.z : uCSMTexel.w;
    float range = (casc == 0) ? uCSMRange.x : (casc == 1) ? uCSMRange.y : (casc == 2) ? uCSMRange.z : uCSMRange.w;
    vec3 lightDir = normalize(uLightDir);
    vec3 nrm = normalize(N);
    float NdotLraw = dot(nrm, -lightDir);
    float baseNormalBias = 1.0 - max(0.0, NdotLraw);
    float nl = max(abs(NdotLraw), 0.15);
    float slopeTan = sqrt(max(0.0, 1.0 - nl * nl)) / nl;
    gSlopeTan = slopeTan;
    gTexelWorld = texel;
    gRange = range;
    gCascId = float(casc);
    vec3 sp0 = (wp - uShadowAnchor);
    {
        vec4 lpProbe = uCSMVP[casc] * vec4(sp0, 1.0);
        if (lpProbe.w > 0.0) gQ = lpProbe.xyz / lpProbe.w * 0.5 + 0.5;
        else gQ = vec3(-1.0);
    }
    if (gQ.x >= 0.0 && gQ.x <= 1.0 && gQ.y >= 0.0 && gQ.y <= 1.0) {
        gD = texture(uCSM, vec3(gQ.xy, float(casc))).r;
        gDiff = gQ.z - gD;
    } else {
        gD = -1.0;
        gDiff = 1.0;
    }
    float texelW = texel;
    gNormalBiasAmt = baseNormalBias * uCSMTune.y * texelW;
    gDepthBiasAmt = uCSMTune.x * range + texelW * slopeTan * uCSMTune.z;
    vec3 sp = (wp - uShadowAnchor);
    vec3 normalBias = nrm * (baseNormalBias * uCSMTune.y * texel);
    normalBias -= lightDir * dot(lightDir, normalBias);
    sp += normalBias;
    float depthBiasAmt = uCSMTune.x * range + texel * slopeTan * uCSMTune.z;
    sp += lightDir * depthBiasAmt;
    sp += lightDir * uCSMTune2.y;
    vec4 lp = uCSMVP[casc] * vec4(sp, 1.0);
    if (lp.w <= 0.0) return 1.0;
    vec3 q = lp.xyz / lp.w * 0.5 + 0.5;
    gQ = q;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0 || q.z < 0.0) return 1.0;
    float dcCenter = texture(uCSM, vec3(q.xy, float(casc))).r;
    gD = dcCenter;
    gDiff = q.z - dcCenter;
    if (uCSMDebug == 1) return 1.0;
    float hardShadow = (q.z >= dcCenter) ? 1.0 : 0.0;
    return hardShadow;
}

layout(location = 0) out vec4 fragColor;

void main() {
    float d = texture(uDepth, vUV).r;
    if (d <= 0.00001) {
        fragColor = vec4(texture(uGAlbedo, vUV).rgb, 1.0);
        return;
    }

    vec4 mat = texture(uGMaterial, vUV);
    if (mat.w < 0.5) {
        fragColor = vec4(texture(uGAlbedo, vUV).rgb, 1.0);
        return;
    }

    vec3 vNrm = texture(uGNormal, vUV).rgb * 2.0 - 1.0;
    if (dot(vNrm, vNrm) < 1e-4) {
        fragColor = vec4(texture(uGAlbedo, vUV).rgb, 1.0);
        return;
    }

    vec4 clip = vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec4 wp = uInvViewProj * clip;
    if (abs(wp.w) < 1e-6) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 vWorldPos = wp.xyz / wp.w;

    vec4 albedoA = texture(uGAlbedo, vUV);
    if (uAlphaTest == 1 && albedoA.a < 0.35) {
        fragColor = vec4(0.0);
        return;
    }
    vec3 albedo = albedoA.rgb;

    vec3 Nw = texture(uGNormal, vUV).rgb * 2.0 - 1.0;
    vec3 N = normalize(Nw);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCamPos - vWorldPos);
    float NdotLraw = dot(N, L);
    float NdotL = max(NdotLraw, 0.0);

    vec3 Hraw = L + V;
    vec3 H = length(Hraw) > 1e-3 ? Hraw / length(Hraw) : N;
    float NdotH = max(dot(N, H), 0.0);
    vec3 specular = vec3(0.0);
    if (uSpecular > 0.5) specular = uLightColor * pow(max(NdotH, 1e-4), uShininess) * step(0.0, NdotLraw) * mat.x;

    float skyOccl = 1.0;
if (uAOOn > 0.5) skyOccl = clamp(texture(uAO, vUV).r, 0.0, 1.0);
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambSky = uAmbientColor;
    vec3 ambGround = uAmbientColor * 0.35;
    vec3 ambient = albedo * uAmbientMul * mix(ambGround, ambSky, hemi);
    ambient *= skyOccl;

    float shadowF = 1.0;
    if (uSun == 1 && uCSMOn == 1 && (NdotL > 0.0 || uCSMDebug >= 1)) {
        float viewZ = length(vWorldPos - uCamPos);
        shadowF = sampleCSM(vWorldPos, N, viewZ);
    }
    if (uCSMDebug == 1) {
        fragColor = vec4(vec3(clamp(gDiff * 500.0 + 0.5, 0.0, 1.0)), 1.0);
        return;
    }
    if (uCSMDebug == 2) {
        fragColor = vec4(vec3(shadowF), 1.0);
        return;
    }
    if (uCSMDebug == 3) {
        float c = gCascId;
        vec3 cc = (c < 0.5) ? vec3(1.0, 0.0, 0.0) : (c < 1.5) ? vec3(0.0, 1.0, 0.0) : (c < 2.5) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 1.0, 0.0);
        fragColor = vec4(cc, 1.0);
        return;
    }
    if (uCSMDebug == 4) {
        fragColor = vec4(fract(gQ.x * 16.0), fract(gQ.y * 16.0), 0.0, 1.0);
        return;
    }
    if (uCSMDebug == 5) {
        fragColor = vec4(vec3(gD), 1.0);
        return;
    }
    if (uCSMDebug == 6) {
        vec3 c = vec3(0.5);
        c.r = clamp(fract(gQ.x * 512.0), 0.0, 1.0);
        c.g = clamp(fract(gQ.y * 512.0), 0.0, 1.0);
        c.b = clamp(gD, 0.0, 1.0);
        fragColor = vec4(c, 1.0);
        return;
    }
    if (uCSMDebug == 9) {
        fragColor = vec4(gCascId / 3.0, gQ.z, gD, clamp(gDiff * 500.0 + 0.5, 0.0, 1.0));
        return;
    }
    if (uCSMDebug == 10) {
        fragColor = vec4(clamp(gTexelWorld * 20.0, 0.0, 1.0), clamp(gRange / 200.0, 0.0, 1.0),
                         clamp(gSlopeTan / 8.0, 0.0, 1.0), clamp(gNormalBiasAmt * 100.0, 0.0, 1.0));
        return;
    }
    if (uCSMDebug == 7) {
        float px = abs(fract(gQ.x * uShadowSize) - 0.5);
        float py = abs(fract(gQ.y * uShadowSize) - 0.5);
        float line = max(step(0.46, px), step(0.46, py));
        vec3 base = vec3(clamp(gDiff * 500.0 + 0.5, 0.0, 1.0));
        fragColor = vec4(mix(base, vec3(1.0, 0.2, 0.2), line), 1.0);
        return;
    }
    if (uCSMDebug == 8) {
        float cx = abs(fract(gQ.x * uShadowSize * 0.25) - 0.5);
        float cy = abs(fract(gQ.y * uShadowSize * 0.25) - 0.5);
        float line = max(step(0.47, cx), step(0.47, cy));
        vec3 base = vec3(clamp(gDiff * 500.0 + 0.5, 0.0, 1.0));
        fragColor = vec4(mix(base, vec3(0.2, 1.0, 0.4), line), 1.0);
        return;
    }
    vec3 sunShade = vec3(shadowF);
    vec3 lit = albedo * uLightColor * NdotL * sunShade + specular * sunShade;

    vec3 gi = vec3(0.0);
    if (uSun == 1) {
        const int giTaps = 12;
        const float giRadius = 1.4;
        const float giStrength = 0.55;
        float gh = quickHash(gl_FragCoord.xy) * 6.28318530718;
        float gsa = sin(gh), gca = cos(gh);
        mat2 gRot = mat2(vec2(gca, -gsa), vec2(gsa, gca));
        vec3 gUp = abs(N.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        vec3 t1 = normalize(cross(N, gUp));
        vec3 t2 = cross(N, t1);
        for (int t = 0; t < giTaps; ++t) {
            float gr = sqrt((float(t) + 0.5) / float(giTaps));
            float ga = float(t) * 2.39996323;
            vec2 gd2 = gRot * vec2(cos(ga), sin(ga));
            vec3 dir = normalize(t1 * gd2.x + t2 * gd2.y + N * (0.3 + 0.7 * gr));
            vec3 sp = vWorldPos + dir * giRadius * gr;
            vec4 cp = uVP * vec4(sp, 1.0);
            if (cp.w <= 0.0) continue;
            vec2 suv = clamp(cp.xy / cp.w * 0.5 + 0.5, vec2(0.0005), vec2(0.9995));
            float sd = texture(uDepth, suv).r;
            if (sd <= 0.00001) continue;
            vec4 cwp = uInvViewProj * vec4(suv * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);
            vec3 swp = cwp.xyz / cwp.w;
            vec3 toS = swp - vWorldPos;
            float sl = length(toS);
            if (sl < 0.12) continue;
            if (dot(toS / sl, dir) < 0.4) continue;
            vec3 sn = texture(uGNormal, suv).rgb * 2.0 - 1.0;
            sn = normalize(sn);
            if (dot(sn, dir) > 0.0) continue;
            vec3 salb = texture(uGAlbedo, suv).rgb;
            float cosW = max(dot(dir, N), 0.0);
            float ncos = max(dot(sn, -toS / sl) * 0.5 + 0.5, 0.0);
            float sLit = max(dot(sn, L), 0.0);
            float sShade = (sLit > 0.0) ? cheapShadow(swp, sn) : 1.0;
            gi += salb * (uLightColor * sLit * sShade * 0.7 + uAmbientColor * 0.35) * cosW * ncos / (1.0 + sl * sl * 1.5);
        }
        gi *= giStrength / float(giTaps);
    }

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
        vec3 pSpec = vec3(0.0);
        if (uSpecular > 0.5) pSpec = plColor * pow(max(pNdotH, 1e-4), uShininess) * step(0.0, pNdotLraw) * mat.x;
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

    vec3 color = ambient + lit + gi;
    vec3 toFrag = vWorldPos - uCamPos;
    float dist2 = length(toFrag);
    float midY = max((uCamPos.y + vWorldPos.y) * 0.5, 0.0);
    float hf = exp(-midY * 0.028);
    float fogAmt = 1.0 - exp(-dist2 * uFogDensity * hf);
    vec3 rd = toFrag / max(dist2, 1e-4);
    float sunAmt = pow(max(dot(rd, L), 0.0), 8.0);
    vec3 fogCol = uFogColor * (0.75 + 0.85 * sunAmt);
    float farEdge = 500.0;
    float farFog = smoothstep(farEdge * 0.85, farEdge, dist2);
    float fogMix = max(clamp(fogAmt, 0.0, 0.92), farFog);
    fragColor = vec4(mix(color, fogCol, fogMix), 1.0 - fogMix * (1.0 - shadowF));
}
