#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragTint;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.25, 0.45, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.0);

    float toonDiffuse;
    if (diffuse > 0.78) {
        toonDiffuse = 1.00;
    } else if (diffuse > 0.48) {
        toonDiffuse = 0.80;
    } else if (diffuse > 0.18) {
        toonDiffuse = 0.58;
    } else {
        toonDiffuse = 0.34;
    }

    vec4 texel = texture(texSampler, fragTexCoord);
    vec3 baseColor = mix(texel.rgb, texel.rgb * fragTint, 0.18);

    vec3 ambient = baseColor * 0.38;
    vec3 lit = ambient + (baseColor * toonDiffuse * 0.82);

    vec3 viewDir = normalize(-fragWorldPos);
    float facing = max(dot(normal, viewDir), 0.0);
    float outline = smoothstep(0.40, 0.95, 1.0 - facing);
    lit = mix(lit, lit * 0.68, outline);

    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = step(0.94, max(dot(normal, halfDir), 0.0));
    lit += vec3(0.22) * spec;

    outColor = vec4(lit, 1.0);
}
