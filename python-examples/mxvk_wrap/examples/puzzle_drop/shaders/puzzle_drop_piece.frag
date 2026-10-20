#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragTint;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in float fragFx;
layout(location = 5) in vec3 fragViewNormal;
layout(location = 6) in vec3 fragViewPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 normal = normalize(fragViewNormal);
    vec3 keyLightDir = normalize(vec3(-0.18, 0.58, 0.80));
    vec3 fillLightDir = normalize(vec3(0.12, 0.08, 0.99));
    vec3 viewDir = normalize(-fragViewPos);
    vec3 reflectDir = reflect(-keyLightDir, normal);
    float keyDiffuse = max(dot(normal, keyLightDir), 0.0);
    float fillDiffuse = max(dot(normal, fillLightDir), 0.0);
    float diffuse = min(keyDiffuse * 0.50 + fillDiffuse * 0.62, 1.0);
    float specular = pow(max(dot(viewDir, reflectDir), 0.0), 54.0);

    vec4 texel = texture(texSampler, vec2(fragTexCoord.x, 1.0 - fragTexCoord.y));
    vec3 baseColor = mix(texel.rgb, texel.rgb * fragTint, 0.22);
    vec3 lit = baseColor * (0.72 + diffuse * 0.38);
    lit += vec3(1.0, 0.94, 0.82) * specular * 0.12;

    float rim = smoothstep(0.45, 1.0, 1.0 - max(dot(normal, viewDir), 0.0));
    lit = mix(lit, lit * 0.86, rim);

    if (fragFx < 0.99) {
        vec3 panel = fragTint * (0.70 + diffuse * 0.20);
        panel += vec3(1.0, 0.94, 0.82) * specular * 0.06;
        outColor = vec4(panel, fragFx);
        return;
    }

    if (fragFx > 1.5) {
        vec3 neon = max(fragTint, vec3(0.08));
        neon /= max(max(neon.r, neon.g), neon.b);
        float neonRim = smoothstep(0.12, 1.0, 1.0 - max(dot(normal, viewDir), 0.0));
        float shaded = 0.50 + diffuse * 0.52;
        lit = neon * shaded;
        lit += neon * neonRim * 0.34;
        lit += neon * 0.12;
    }

    outColor = vec4(lit, 1.0);
}
