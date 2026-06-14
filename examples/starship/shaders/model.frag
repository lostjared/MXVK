#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 lightPos = vec3(2.5, 2.0, 3.5);
    vec3 lightDir = normalize(lightPos - fragWorldPos);
    vec3 viewDir = normalize(ubo.fx.xyz - fragWorldPos);
    float diffuse = max(dot(N, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, N);
    float specular = pow(max(dot(viewDir, reflectDir), 0.0), 48.0);
    float lighting = 0.16 + diffuse * 0.70 + specular * 0.45;

    vec4 base = texture(texSampler, fragTexCoord);
    if (base.a <= 0.0001) {
        base = vec4(0.96, 0.97, 1.0, 1.0);
    }

    outColor = vec4(base.rgb * lighting, 1.0);
}
