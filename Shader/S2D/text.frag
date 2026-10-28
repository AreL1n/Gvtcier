#version 460 core
in vec2 vUV;
uniform sampler2D uFont;
uniform vec4 uColor;
layout(location = 0) out vec4 fragColor;

void main() {
    float a = texture(uFont, vUV).r;
    if (a < 0.003) discard;
    fragColor = vec4(uColor.rgb, uColor.a * a);
}
