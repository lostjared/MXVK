#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragTint;
layout(location = 3) out vec3 fragWorldPos;
layout(location = 4) out float fragFx;
layout(location = 5) out vec3 fragViewNormal;
layout(location = 6) out vec3 fragViewPos;

layout(binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 fx;
} pc;

void main() {
    fragTexCoord = inTexCoord;
    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTint = pc.fx.rgb;
    fragFx = pc.fx.a;
    fragWorldPos = (pc.model * vec4(inPosition, 1.0)).xyz;
    fragViewPos = (ubo.view * pc.model * vec4(inPosition, 1.0)).xyz;
    fragViewNormal = normalize(transpose(inverse(mat3(ubo.view * pc.model))) * inNormal);
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(inPosition, 1.0);
}
