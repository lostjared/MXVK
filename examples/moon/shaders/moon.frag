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

vec3 spectrum(float t) {
    return vec3(0.5) + 0.5 * cos(6.28318 * (t + vec3(0.0, 0.33, 0.67)));
}

vec2 mirrorUV(vec2 uv) {
    return 1.0 - abs(1.0 - 2.0 * fract(uv));
}

vec4 sampleMirrored(vec2 uv) {
    vec2 texSize = vec2(textureSize(texSampler, 0));
    vec2 eps = 0.5 / texSize;
    return texture(texSampler, clamp(mirrorUV(uv), eps, 1.0 - eps));
}

vec4 sampleTriplanar(vec3 p, vec2 offset) {
    vec3 n = normalize(fragLocalPos + vec3(0.0001));
    vec3 weight = pow(abs(n), vec3(4.0));
    weight /= max(weight.x + weight.y + weight.z, 0.0001);

    float texScale = 0.72;
    vec4 xProj = sampleMirrored(p.zy * texScale + 0.5 + offset);
    vec4 yProj = sampleMirrored(p.xz * texScale + 0.5 + offset);
    vec4 zProj = sampleMirrored(p.xy * texScale + 0.5 + offset);

    return xProj * weight.x + yProj * weight.y + zProj * weight.z;
}

void main() {
    float time_f = ubo.fx.x;

    vec3 localDir = normalize(fragLocalPos + vec3(0.0001));
    vec2 uv = localDir.xy;

    float r = length(uv);
    float angle = atan(uv.y, uv.x);

    float ripple = sin(angle * 10.0 + time_f) * 0.045;
    ripple += sin(angle * 25.0 - time_f * 2.0) * 0.018;

    float wave = sin(r * 26.0 - time_f * 4.8 + ripple * 10.0);
    float shift = ripple * 0.5 + wave * 0.012;

    vec3 samplePos = localDir + vec3(ripple * 0.25, wave * 0.015, ripple * 0.18);
    float r_chan = sampleTriplanar(samplePos, vec2(shift, 0.0)).r;
    float g_chan = sampleTriplanar(samplePos, vec2(0.0)).g;
    float b_chan = sampleTriplanar(samplePos, vec2(-shift, 0.0)).b;
    vec3 baseTex = vec3(r_chan, g_chan, b_chan);

    vec3 rainbow = spectrum(r - time_f * 0.5 + ripple);
    float glowMask = smoothstep(0.5, 1.0, wave);

    float center = exp(-r * 3.8);
    vec3 coreGlow = vec3(1.0, 0.98, 0.9) * center * 1.8;

    vec3 finalColor = mix(baseTex, rainbow, glowMask * 0.42);
    finalColor += coreGlow;
    finalColor += vec3(wave * ripple * 2.8);

    vec3 n = normalize(fragNormal);
    vec3 v = normalize(-fragViewPos);
    vec3 l = normalize(vec3(0.0, 10.0, 6.0) - fragViewPos);
    vec3 h = normalize(l + v);
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 36.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 2.0) * 0.12;
    float lighting = clamp(0.46 + diffuse * 0.46 + specular * 0.14 + rim, 0.30, 1.14);

    outColor = vec4(clamp(finalColor * lighting, vec3(0.0), vec3(1.0)), 1.0);
}
