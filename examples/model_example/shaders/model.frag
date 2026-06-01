#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.0, 0.2, 1.0));
    float diffuse = max(dot(N, lightDir), 0.0);
    float lighting = 0.55 + diffuse * 0.45;

    vec4 base = texture(texSampler, fragTexCoord);
    if (base.a <= 0.0001) {
        base = vec4(0.96, 0.97, 1.0, 1.0);
    }

    outColor = vec4(base.rgb * lighting, 1.0);
}
