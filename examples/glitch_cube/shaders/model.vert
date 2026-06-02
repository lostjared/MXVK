#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 vertexColor;
layout(location = 1) out vec2 TexCoords;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    vec3 lightPos = vec3(0.0, 5.0, 0.0);
    vec3 cameraPos = vec3(inverse(ubo.view)[3]);
    vec3 lightColor = vec3(2.0, 2.0, 2.0);
    vec3 objectColor = vec3(1.0, 1.0, 1.0);

    vec3 norm = normalize(mat3(transpose(inverse(ubo.model))) * inNormal);
    vec3 lightDir = normalize(lightPos - worldPos.xyz);

    float ambientStrength = 0.8;
    vec3 ambient = ambientStrength * lightColor;

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    float specularStrength = 1.0;
    float shininess = 64.0;
    vec3 viewDir = normalize(cameraPos - worldPos.xyz);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    vec3 specular = specularStrength * spec * lightColor;

    vec3 finalColor = (ambient + diffuse + specular) * objectColor;
    vertexColor = finalColor;
    TexCoords = inTexCoord;
}
