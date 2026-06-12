#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragViewPos;
layout(location = 2) out vec3 fragViewNormal;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    vec4 viewPos = ubo.view * worldPos;
    mat3 normalMatrix = transpose(inverse(mat3(ubo.view * ubo.model)));

    fragTexCoord = inTexCoord;
    fragViewPos = viewPos.xyz;
    fragViewNormal = normalize(normalMatrix * inNormal);
    gl_Position = ubo.proj * viewPos;
}
