#version 460 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uTint;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 c = texture(uTex, vUV);
    fragColor = c * uTint;
}
