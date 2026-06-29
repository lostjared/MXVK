#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragTint;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in float fragFx;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.25, 0.45, 1.0));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float band = diffuse > 0.72 ? 1.0 : (diffuse > 0.38 ? 0.74 : 0.45);

    vec4 texel = texture(texSampler, vec2(fragTexCoord.x, 1.0 - fragTexCoord.y));
    vec3 baseColor = mix(texel.rgb, texel.rgb * fragTint, 0.22);
    vec3 lit = baseColor * (0.36 + band * 0.74);

    vec3 viewDir = normalize(-fragWorldPos);
    float rim = smoothstep(0.45, 1.0, 1.0 - max(dot(normal, viewDir), 0.0));
    lit = mix(lit, lit * 0.62, rim);

    if (fragFx < 0.99) {
        vec3 panel = fragTint * (0.72 + band * 0.18);
        outColor = vec4(panel, fragFx);
        return;
    }

    if (fragFx > 1.5) {
        vec3 neon = max(fragTint, vec3(0.08));
        neon /= max(max(neon.r, neon.g), neon.b);
        float neonRim = smoothstep(0.12, 1.0, 1.0 - max(dot(normal, viewDir), 0.0));
        float shaded = 0.30 + band * 0.72;
        lit = neon * shaded;
        lit += neon * neonRim * 0.34;
        lit += neon * 0.12;
    }

    outColor = vec4(lit, 1.0);
}
