#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragViewPos;
layout(location = 2) in vec3 fragViewNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 base = texture(texSampler, fragTexCoord);
    if (base.a <= 0.0001) {
        base = vec4(0.96, 0.97, 1.0, 1.0);
    }

    vec3 N = normalize(fragViewNormal);
    vec3 L = normalize(vec3(-0.6, 0.8, 1.2));
    vec3 V = normalize(-fragViewPos);
    vec3 R = reflect(-L, N);

    float ambient = 0.18;
    float diffuse = max(dot(N, L), 0.0);
    float specular = 0.0;
    if (diffuse > 0.0) {
        specular = pow(max(dot(V, R), 0.0), 32.0);
    }

    vec3 litColor = base.rgb * (ambient + diffuse * 0.9) + vec3(1.0) * (specular * 0.35);
    outColor = vec4(litColor, base.a);
}
