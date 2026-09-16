#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragTint;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in vec3 fragViewNormal;
layout(location = 5) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 lightPos = vec3(0.0, 12.0, 10.0);
    vec3 viewPos = vec3(0.0, 0.0, 10.0);
    vec3 lightColor = vec3(1.65);

    float ambientStrength = 0.15;
    vec3 ambient = ambientStrength * lightColor;

    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragWorldPos);
    float diff = max(abs(dot(norm, lightDir)), 0.0);
    vec3 diffuse = diff * lightColor;

    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - fragWorldPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(abs(dot(viewDir, reflectDir)), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;

    vec4 texel = texture(texSampler, fragTexCoord);
    vec3 result = (ambient + diffuse + specular) * texel.rgb * fragTint;
    outColor = vec4(result, texel.a);
}
