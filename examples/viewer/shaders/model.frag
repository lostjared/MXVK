#version 450

layout(location = 0) in vec3 fragViewPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

void main() {
    vec3 lightPosView = (ubo.view * vec4(0.0, 10.0, 5.0, 1.0)).xyz;
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(lightPosView - fragViewPos);
    vec3 viewDir = normalize(-fragViewPos);

    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 reflected = reflect(-lightDir, normal);
    float specular = pow(max(dot(viewDir, reflected), 0.0), 32.0) * 0.2;

    vec4 texel = texture(texSampler, fragTexCoord);
    if (texel.a <= 0.0001) {
        texel = vec4(0.9, 0.92, 0.96, 1.0);
    }

    vec3 lit = texel.rgb * (0.3 + diffuse) + vec3(specular);
    outColor = vec4(clamp(lit, vec3(0.0), vec3(1.0)), 1.0);
}
