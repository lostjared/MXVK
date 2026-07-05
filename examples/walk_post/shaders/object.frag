#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 lightPos = vec3(0.0, 15.0, 0.0);
    vec3 viewPos = fragFx.xyz;
    vec3 lightColor = vec3(1.0);

    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragWorldPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - fragWorldPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);
    vec3 specular = specularStrength * spec * lightColor;

    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 result = (ambient + diffuse + specular) * texColor.rgb;
    outColor = vec4(result, texColor.a);
}
