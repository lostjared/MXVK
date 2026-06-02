#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragTint;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.1, 0.35, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.0);

    vec4 texel = texture(texSampler, fragTexCoord);
    // Always show texture detail and apply only a subtle per-object tint.
    vec3 tintMix = mix(vec3(1.0), fragTint, 0.30);
    vec3 baseColor = texel.rgb * tintMix;

    vec3 ambient = baseColor * 0.50;
    vec3 lit = ambient + (baseColor * diffuse * 0.70);

    vec3 viewDir = normalize(-fragWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 48.0);
    lit += vec3(0.28) * spec;

    outColor = vec4(lit, 1.0);
}
