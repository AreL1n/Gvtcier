#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;
layout(location = 3) in float aRotation;
layout(location = 4) in vec2 aAgeKind;
layout(location = 5) in float aFacing;
layout(location = 6) in vec3 aVel;
#include "gvu.glsl"
out vec4 vColor;
out vec2 vUv;
out float vAge;
out float vKind;

const vec2 kCorner[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
);

const vec2 kUv[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vColor = aColor;
    vAge = aAgeKind.x;
    vKind = aAgeKind.y;

    if (aFacing > 2.5) {
        int idx = gl_VertexID - (gl_VertexID / 6) * 6;
        vUv = kUv[idx];
        gl_Position = uProj * uView * vec4(aPos, 1.0);
        return;
    }

    int idx = gl_VertexID - (gl_VertexID / 6) * 6;
    vec2 corner = kCorner[idx];

    float cs = cos(aRotation);
    float sn = sin(aRotation);
    vec2 rc = vec2(corner.x * cs - corner.y * sn,
                   corner.x * sn + corner.y * cs);

    vec3 right = normalize(vec3(uView[0][0], uView[1][0], uView[2][0]));
    vec3 up = normalize(vec3(uView[0][1], uView[1][1], uView[2][1]));

    if (aFacing > 1.5) {
        vec3 dir = aVel;
        float len = length(dir);
        if (len > 1e-4) {
            dir /= len;
            vec3 side = cross(dir, normalize(uCamPos - aPos));
            float sl = length(side);
            if (sl > 1e-4) {
                vec3 axis = side / sl;
                right = normalize(cross(axis, dir));
                up = dir;
            }
        }
    } else if (aFacing > 0.5) {
        vec3 viewDir = normalize(uCamPos - aPos);
        vec3 horiz = vec3(viewDir.x, 0.0, viewDir.z);
        float hl = length(horiz);
        if (hl > 1e-4) horiz /= hl;
        right = normalize(cross(vec3(0.0, 1.0, 0.0), horiz));
        up = vec3(0.0, 1.0, 0.0);
    }

    vec3 world = aPos + (right * rc.x + up * rc.y) * aSize;

    vUv = kUv[idx];
    gl_Position = uProj * uView * vec4(world, 1.0);
}
