#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out float fragDistance;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 params;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragDistance = clamp((inPosition.z - 0.555) / 0.55, 0.0, 1.0);
}
