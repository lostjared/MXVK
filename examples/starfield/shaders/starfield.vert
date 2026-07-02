#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in float inSize;
layout(location = 2) in vec4 inColor;

layout(binding = 1) uniform UniformBufferObject {
    mat4 mvp;
} ubo;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
    gl_PointSize = inSize;
    fragColor = inColor;
}
