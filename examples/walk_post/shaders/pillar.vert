#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragFx;
layout(location = 3) out vec3 fragWorldPos;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pushConstants;

void main() {
    vec4 worldPos = pushConstants.model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(pushConstants.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inTexCoord;
    fragFx = ubo.fx;
    fragWorldPos = worldPos.xyz;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
