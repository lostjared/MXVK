#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragViewPos;
layout(location = 3) in vec3 fragLocalPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

float pingPong(float x, float lengthVal) {
    float m = mod(x, lengthVal * 2.0);
    return m <= lengthVal ? m : lengthVal * 2.0 - m;
}

vec4 sampleWrappedUV(vec2 uv) {
    uv = vec2(fract(uv.x), clamp(uv.y, 0.0, 1.0));

    vec4 base = texture(texSampler, uv);
    vec4 left = texture(texSampler, vec2(fract(uv.x + 0.018), uv.y));
    vec4 right = texture(texSampler, vec2(fract(uv.x - 0.018), uv.y));

    float edge = max(smoothstep(0.035, 0.0, uv.x), smoothstep(0.965, 1.0, uv.x));
    return mix(base, (base + left + right) / 3.0, edge);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(-fragViewPos);

    float time_f = ubo.fx.x;
    float fade = clamp(ubo.fx.w, 0.0, 1.0);

    vec4 baseColor = sampleWrappedUV(fragTexCoord);

    vec2 effectSource = fragTexCoord * 2.0 - 1.0;
    float len = length(effectSource);
    float time_t = max(pingPong(time_f, 10.0), 0.35);
    float bubble = smoothstep(0.8, 1.0, 1.0 - len);
    bubble = sin(bubble * time_t);

    vec2 distorted = effectSource * (1.0 + 0.1 * sin(time_f + len * 20.0));
    distorted = sin(distorted * time_t);

    vec2 effectUv = clamp(distorted * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    effectUv = mix(fragTexCoord, effectUv, 0.35);
    vec4 effectColor = sampleWrappedUV(effectUv);

    vec3 whiteMix = mix(effectColor.rgb, vec3(1.0), clamp(bubble, 0.0, 1.0));
    vec3 effectRgb = mix(effectColor.rgb, whiteMix, 0.45);

    float crystalMask = smoothstep(0.95, 0.05, length(fragTexCoord * 2.0 - 1.0));
    crystalMask = clamp(crystalMask + 0.05 * sin(fragLocalPos.z * 0.45), 0.0, 1.0);

    vec3 lightPos = vec3(0.0, 10.0, 6.0);
    vec3 L = normalize(lightPos - fragViewPos);
    vec3 H = normalize(L + V);

    float ambient = 0.46;
    float diffuse = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, H), 0.0), 42.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0) * 0.10;
    float fill = 0.16 * max(dot(N, vec3(0.0, 0.0, 1.0)), 0.0);

    float lighting = ambient + fill + (0.62 * diffuse) + (0.16 * specular) + rim;
    lighting = clamp(lighting, 0.28, 1.08);

    vec3 crystalTint = mix(baseColor.rgb, effectRgb, 0.38);
    crystalTint = mix(crystalTint, vec3(0.82, 0.96, 1.0), crystalMask * 0.08);
    vec3 litColor = crystalTint * lighting;
    litColor = pow(max(litColor, vec3(0.0)), vec3(1.08));

    outColor = vec4(litColor * fade, baseColor.a * fade);
}
