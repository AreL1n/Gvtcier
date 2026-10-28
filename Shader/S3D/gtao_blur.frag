#version 460 core
in vec2 vUV;
uniform sampler2D uAO;
uniform sampler2D uEdges;
#include "gvu.glsl"
layout(location=0) out vec4 fragColor;

vec4 unpackEdges(float packedVal) {
    uint p = uint(packedVal * 255.5);
    vec4 e;
    e.x = float((p >> 6) & 0x03) / 3.0;
    e.y = float((p >> 4) & 0x03) / 3.0;
    e.z = float((p >> 2) & 0x03) / 3.0;
    e.w = float((p >> 0) & 0x03) / 3.0;
    return clamp(e, 0.0, 1.0);
}

void main() {
    vec2 texel = 1.0 / uRes;

    vec4 eC = unpackEdges(texture(uEdges, vUV).r);
    vec4 eL = unpackEdges(texture(uEdges, vUV - vec2(texel.x, 0.0)).r);
    vec4 eR = unpackEdges(texture(uEdges, vUV + vec2(texel.x, 0.0)).r);
    vec4 eT = unpackEdges(texture(uEdges, vUV - vec2(0.0, texel.y)).r);
    vec4 eB = unpackEdges(texture(uEdges, vUV + vec2(0.0, texel.y)).r);

    eC *= vec4(eL.y, eR.x, eT.w, eB.z);

    float edginess = (clamp(4.0 - 2.5 - (eC.x + eC.y + eC.z + eC.w), 0.0, 1.5) / 1.5) * 0.5;
    eC = clamp(eC + edginess, 0.0, 1.0);

    float diagWeight = 0.85 * 0.5;
    float wTL = diagWeight * (eC.x * eL.z + eC.z * eT.x);
    float wTR = diagWeight * (eC.z * eT.y + eC.y * eR.z);
    float wBL = diagWeight * (eC.w * eB.x + eC.x * eL.w);
    float wBR = diagWeight * (eC.y * eR.w + eC.w * eB.y);

    float center = texture(uAO, vUV).r;
    float sum = center * uBeta;
    float wsum = uBeta;

    float aoL = texture(uAO, vUV - vec2(texel.x, 0.0)).r;
    float aoR = texture(uAO, vUV + vec2(texel.x, 0.0)).r;
    float aoT = texture(uAO, vUV - vec2(0.0, texel.y)).r;
    float aoB = texture(uAO, vUV + vec2(0.0, texel.y)).r;

    sum += aoL * eC.x; wsum += eC.x;
    sum += aoR * eC.y; wsum += eC.y;
    sum += aoT * eC.z; wsum += eC.z;
    sum += aoB * eC.w; wsum += eC.w;

    float aoTL = texture(uAO, vUV - texel).r;
    float aoTR = texture(uAO, vUV + vec2(texel.x, -texel.y)).r;
    float aoBL = texture(uAO, vUV + vec2(-texel.x, texel.y)).r;
    float aoBR = texture(uAO, vUV + texel).r;

    sum += aoTL * wTL; wsum += wTL;
    sum += aoTR * wTR; wsum += wTR;
    sum += aoBL * wBL; wsum += wBL;
    sum += aoBR * wBR; wsum += wBR;

    float ao = sum / max(wsum, 1e-5);
    fragColor = vec4(ao, ao, ao, 1.0);
}
