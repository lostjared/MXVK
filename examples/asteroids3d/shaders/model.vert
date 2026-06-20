#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

void main() {
    mat3 normalMatrix = transpose(inverse(mat3(ubo.model)));
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);

    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inTexCoord;
    fragWorldPos = worldPos.xyz;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
