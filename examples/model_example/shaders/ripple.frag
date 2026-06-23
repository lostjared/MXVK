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

float getRippleHeight(vec2 p, float t) {
    vec2 src1 = vec2(sin(t * 0.7) * 0.3, cos(t * 0.5) * 0.2);
    vec2 src2 = vec2(-sin(t * 0.6) * 0.25, -cos(t * 0.8) * 0.3);
    vec2 src3 = vec2(0.0);

    float d1 = length(p - src1);
    float d2 = length(p - src2);
    float d3 = length(p - src3);

    // Replaced audio multipliers with static variables to maintain the dynamic look
    float w1 = sin(d1 * 25.0 - t * 5.0) / (1.0 + d1 * 3.0);
    float w2 = sin(d2 * 22.0 - t * 4.0) / (1.0 + d2 * 3.0);
    float w3 = sin(d3 * 27.0 - t * 6.0) / (1.0 + d3 * 3.0);

    return (w1 + w2 + w3) * 0.5;
}

void main() {
    float time_f = ubo.fx.x;
    
    vec3 localDir = normalize(fragLocalPos + vec3(0.0001));
    vec2 uv = localDir.xy;
    float r = length(uv);

    // Surface tension & fake normal generation
    float surface = getRippleHeight(uv, time_f);
    
    vec2 dx = vec2(0.01, 0.0);
    vec2 dy = vec2(0.0, 0.01);
    float hx = getRippleHeight(uv + dx, time_f);
    float hy = getRippleHeight(uv + dy, time_f);
    
    vec2 distNorm = vec2(hx - surface, hy - surface) * 4.0;

    // Refract texture through liquid metal using triplanar offset
    vec3 samplePos = localDir + vec3(distNorm * 0.05, surface * 0.02);

    // Chromatic split for metallic refraction
    float chroma = 0.02;
    float r_chan = sampleTriplanar(samplePos, vec2(chroma, 0.0)).r;
    float g_chan = sampleTriplanar(samplePos, vec2(0.0)).g;
    float b_chan = sampleTriplanar(samplePos, vec2(-chroma, 0.0)).b;
    vec3 baseTex = vec3(r_chan, g_chan, b_chan);

    // Mercury metallic tint: desaturate + silver
    float luma = dot(baseTex, vec3(0.299, 0.587, 0.114));
    vec3 mercury = mix(vec3(luma) * vec3(0.9, 0.92, 0.95), baseTex, 0.4);

    // Rainbow on ripple crests
    float crest = smoothstep(0.2, 0.5, surface);
    vec3 rainbow = spectrum(surface * 2.0 + time_f * 0.2 + r);
    mercury = mix(mercury, mercury * rainbow, crest * 0.6);

    // Central glow
    float center = exp(-r * 5.0);
    mercury += vec3(0.95, 0.97, 1.0) * center * 1.5;

    // Ripple highlights
    mercury += surface * surface * 0.4;

    vec3 n = normalize(fragNormal);
    vec3 v = normalize(-fragViewPos);
    vec3 l = normalize(vec3(0.0, 10.0, 6.0) - fragViewPos);
    vec3 h = normalize(l + v);
    
    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 36.0);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 2.0) * 0.12;
    float lighting = clamp(0.46 + diffuse * 0.46 + specular * 0.14 + rim, 0.30, 1.14);

    outColor = vec4(clamp(mercury * lighting, vec3(0.0), vec3(1.0)), 1.0);
}
