#version 460 core
in vec4 vColor;
in vec2 vUv;
in float vAge;
in float vKind;
layout(location = 0) out vec4 fragColor;

float hash21(vec2 p) {
    p = fract(p * vec2(233.34, 851.73));
    p += dot(p, p + 23.45);
    return fract(p.x * p.y);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float s = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        s += noise2(p) * amp;
        p *= 2.03;
        amp *= 0.5;
    }
    return s;
}

void main() {
    vec2 uv = vUv;
    vec2 pc = uv - vec2(0.5);
    float d = length(pc) * 2.0;

    if (vKind > 1.5) {
        float lat = abs(uv.x - 0.5) * 2.0;
        float edge = max(0.0, 1.0 - lat);
        edge = edge * edge * edge;
        float along = smoothstep(0.0, 0.12, uv.y) * smoothstep(0.0, 0.12, 1.0 - uv.y);
        float a = vColor.a * edge * mix(0.55, 1.0, along);
        if (a < 0.004) discard;
        float glow = mix(1.0, 0.55, lat * lat);
        vec3 col = vColor.rgb * glow + vec3(1.0) * (1.0 - lat) * 0.35;
        fragColor = vec4(col * a, a);
        return;
    }

    float age = clamp(vAge, 0.0, 1.0);
    float turb = fbm(uv * 3.2 + vec2(age * 1.6, -age * 2.4));
    float turb2 = fbm(uv * 6.1 - vec2(age * 2.2, age * 1.1));

    if (vKind > 0.5) {
        vec2 sc = uv * 2.2 + vec2(age * 0.8, -age * 1.5);
        float n1 = fbm(sc);
        float n2 = fbm(sc * 2.4 + vec2(-age * 1.1, age * 0.6));
        float n3 = fbm(sc * 5.1 + vec2(age * 0.9, -age * 0.4));
        float shape = n1 * 0.52 + n2 * 0.31 + n3 * 0.17;

        float radial = clamp(1.0 - d, 0.0, 1.0);
        float falloff = radial * radial * radial;

        float wobble = 0.16 * sin(uv.x * 9.4 + age * 2.6) * sin(uv.y * 7.7 - age * 1.9);
        float dens = shape * 0.95 + falloff * 0.55 + wobble - 0.52;
        dens *= mix(1.15, 0.42, age);

        float a = smoothstep(0.0, 0.42, dens) * vColor.a;
        a *= smoothstep(1.0, 0.70, d);
        a = min(a, 0.55);
        if (a < 0.008) discard;

        float lit = mix(0.66, 1.05, shape);
        float shade = lit * mix(1.10, 0.80, age);
        fragColor = vec4(vec3(shade), a);
        return;
    }

    float core = max(0.0, 1.0 - d);
    float soft = core * core;
    float mask = soft * mix(0.6, 1.4, turb) * (1.0 - age * 0.55);
    float a = vColor.a * clamp(mask, 0.0, 1.0) * (1.0 - age * 0.9);
    a *= smoothstep(1.0, 0.15, d);
    if (a < 0.004) discard;
    float heat = clamp(mix(1.35, 0.25, age) * mix(0.85, 1.15, turb2), 0.0, 1.6);
    vec3 base = vColor.rgb;
    vec3 col = base * heat;
    col += vec3(1.0, 0.85, 0.35) * clamp(1.0 - age * 3.0, 0.0, 1.0) * 0.75 * soft;
    fragColor = vec4(col * a, a);
}
