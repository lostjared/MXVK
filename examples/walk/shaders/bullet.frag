#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;

layout(location = 0) out vec4 outColor;

void main() {
    float alpha = clamp(fragFx.x, 0.0, 1.0);
    outColor = vec4(1.0, 0.1, 0.0, alpha);
}
