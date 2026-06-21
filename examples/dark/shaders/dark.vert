#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragViewPos;
layout(location = 3) out vec3 fragLocalPos;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

void main() {
    mat4 modelView = ubo.view * ubo.model;
    mat3 normalMatrix = transpose(inverse(mat3(modelView)));
    vec4 viewPos = modelView * vec4(inPosition, 1.0);

    fragTexCoord = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragViewPos = viewPos.xyz;
    fragLocalPos = inPosition;
    gl_Position = ubo.proj * viewPos;
}
