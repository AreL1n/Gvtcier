#version 460 core
in vec3 vRel;
layout(location = 0) out float fragDist;

void main() {
    fragDist = length(vRel);
}
