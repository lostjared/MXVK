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
    vec3 normal = normalize(fragNormal);
    vec3 lightPos = vec3(7.0, 6.0, 9.0);
    vec3 lightDir = normalize(lightPos - fragWorldPos);
    vec3 viewDir = normalize(ubo.fx.xyz - fragWorldPos);

    float ambient = 0.18;
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, normal);
    float specular = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    float lighting = ambient + diffuse * 0.72 + specular * 0.42;

    vec4 base = texture(texSampler, fragTexCoord);
    if (base.a <= 0.0) {
        base = vec4(1.0);
    }

    outColor = vec4(base.rgb * lighting, base.a);
}
