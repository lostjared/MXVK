#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragViewPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

void main() {
    vec4 viewPos = ubo.view * ubo.model * vec4(inPosition, 1.0);
    fragViewPos = viewPos.xyz;
    fragNormal = normalize(transpose(inverse(mat3(ubo.view * ubo.model))) * inNormal);
    fragTexCoord = inTexCoord;
    gl_Position = ubo.proj * viewPos;
}
