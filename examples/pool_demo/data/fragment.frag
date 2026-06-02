#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragTint;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

vec3 lampBulb(vec3 bulbPos, vec3 lampCol, vec3 worldPos, vec3 normal, vec3 baseCol) {
    vec3 lv = bulbPos - worldPos;
    float dist = length(lv);
    vec3 ldir = lv / max(dist, 0.001);

    float diff = max(dot(normal, ldir), 0.0);
    float atten = 1.0 / (1.0 + 0.18 * dist + 0.12 * dist * dist);

    vec3 viewDir = normalize(-worldPos);
    vec3 halfDir = normalize(ldir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 96.0);
    vec3 specular = 0.5 * spec * lampCol;

    return (lampCol * diff * baseCol + specular) * atten;
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.0, 0.2, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.0);

    vec4 texel = texture(texSampler, fragTexCoord);
    float texStrength = step(0.02, dot(texel.rgb, vec3(1.0 / 3.0)));
    vec3 baseColor = mix(fragTint, fragTint * texel.rgb, texStrength);

    vec3 lampCol = vec3(1.0, 0.92, 0.72);
    float lampY = 2.5;

    vec3 lampLight = vec3(0.0);
    lampLight += lampBulb(vec3(-4.2, lampY, 0.0), lampCol, fragWorldPos, normal, baseColor);
    lampLight += lampBulb(vec3(0.0, lampY, 0.0), lampCol, fragWorldPos, normal, baseColor);
    lampLight += lampBulb(vec3(4.2, lampY, 0.0), lampCol, fragWorldPos, normal, baseColor);
    lampLight *= 3.8;

    vec3 fillDir = normalize(vec3(0.3, -0.6, 0.5));
    float fillDiff = max(dot(normal, fillDir), 0.0);
    vec3 fill = 0.30 * fillDiff * baseColor * vec3(0.6, 0.7, 1.0);

    vec3 skyCol = vec3(0.42, 0.37, 0.28);
    vec3 groundCol = vec3(0.28, 0.30, 0.38);
    float hemi = dot(normal, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    vec3 ambient = mix(groundCol, skyCol, hemi) * baseColor;

    float baseline = 0.45 + diffuse * 0.15;
    vec3 result = (ambient + fill + lampLight) * baseline;

    outColor = vec4(result, 1.0);
}
