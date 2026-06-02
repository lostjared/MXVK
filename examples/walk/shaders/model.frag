#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 base = texture(texSampler, fragTexCoord);
    if (base.a <= 0.0001) {
        base = vec4(0.92, 0.93, 0.96, 1.0);
    }

    vec3 n = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.1 + 0.2 * sin(fragFx.w * 0.65), 0.85, 0.25));
    float diff = max(dot(n, lightDir), 0.0);
    float ambient = 0.38;
    float lighting = ambient + diff * 0.62;

    vec3 tint = mix(vec3(1.0), fragFx.rgb, 0.55);
    outColor = vec4(base.rgb * tint * lighting, 1.0);
}
