#version 450

layout(push_constant) uniform PushConstants {
    mat4 u_viewProjection;
    vec4 u_cameraTime;
} pc;

layout(location = 0) in vec3 v_worldPos;
layout(location = 1) in vec2 v_texCoord;
layout(location = 2) in vec3 v_baseNormal;
layout(location = 3) in float v_height;
layout(location = 4) in vec4 v_color;

layout(location = 0) out vec4 outColor;

float wave_height(vec2 p, vec2 dir, float steepness, float wavelength, float speed, float time) {
    float k = 6.2831853 / wavelength;
    float phase = k * dot(normalize(dir), p) + speed * time;
    float a = steepness / k;
    return a * sin(phase);
}

float area_variation(vec2 p) {
    float a = sin(dot(p, vec2(0.073, 0.041)));
    float b = cos(dot(p, vec2(-0.037, 0.089)));
    float c = sin(dot(p, vec2(0.019, -0.061)) + a * 1.7);
    return 0.78 + 0.28 * (a * 0.45 + b * 0.35 + c * 0.20);
}

vec2 warped_position(vec2 p, float time) {
    vec2 warp;
    warp.x = sin(dot(p, vec2(0.031, 0.047)) + time * 0.12);
    warp.y = cos(dot(p, vec2(-0.043, 0.029)) - time * 0.08);
    return p + warp * 2.35;
}

float combined_height(vec2 p, float time) {
    vec2 q = warped_position(p, time);
    float local = area_variation(p);
    float height = 0.0;
    height += wave_height(q, vec2(1.0, 0.25), 0.40 * local, 5.6, 1.10, time);
    height += wave_height(q + vec2(1.8, -0.7), vec2(-0.35, 1.0), 0.29 * (1.18 - local * 0.18), 3.3, 1.55, time);
    height += wave_height(p, vec2(0.8, -0.65), 0.18 * local, 1.7, 2.35, time);
    height += wave_height(q, vec2(-0.9, -0.2), 0.12, 2.2, 1.95, time);
    height += sin(dot(p, vec2(0.19, -0.13)) + sin(dot(p, vec2(-0.047, 0.062))) * 1.4 + time * 0.52) * 0.13;
    height += sin((q.x - q.y) * 3.4 + time * 1.9) * 0.030;
    return height;
}

vec3 smooth_wave_normal(vec2 p, float time) {
    float e = 0.18;
    float h0 = combined_height(p, time);
    float hx = combined_height(p + vec2(e, 0.0), time);
    float hz = combined_height(p + vec2(0.0, e), time);
    return normalize(vec3(-(hx - h0) / e, 1.0, -(hz - h0) / e));
}

float wrappedDelta(float value) {
    return min(abs(value), 1.0 - abs(value));
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.52;
    for (int i = 0; i < 6; ++i) {
        value += noise(p) * amplitude;
        p = mat2(1.74, 0.38, -0.42, 1.61) * p + vec2(0.17, -0.11);
        amplitude *= 0.52;
    }
    return value;
}

float softPuff(vec2 uv, vec2 center, vec2 radius) {
    vec2 d = vec2(wrappedDelta(uv.x - center.x), uv.y - center.y);
    float q = dot(d / radius, d / radius);
    return exp(-q * 1.85);
}

float cloudGroup(vec2 uv, vec2 center, float scale, float time, float seed) {
    float c = 0.0;
    c += softPuff(uv, center + vec2(-0.17, -0.030) * scale, vec2(0.070, 0.028) * scale) * 0.55;
    c += softPuff(uv, center + vec2(-0.10, 0.000) * scale, vec2(0.090, 0.040) * scale);
    c += softPuff(uv, center + vec2(-0.035, 0.038) * scale, vec2(0.090, 0.052) * scale);
    c += softPuff(uv, center + vec2(0.030, 0.055) * scale, vec2(0.100, 0.058) * scale);
    c += softPuff(uv, center + vec2(0.105, 0.025) * scale, vec2(0.085, 0.042) * scale);
    c += softPuff(uv, center + vec2(0.165, -0.010) * scale, vec2(0.065, 0.030) * scale) * 0.78;
    c += softPuff(uv, center + vec2(0.015, -0.032) * scale, vec2(0.170, 0.022) * scale) * 0.55;

    vec2 drift = vec2(time * 0.018, time * 0.003);
    float broad = fbm(uv * vec2(8.0, 4.2) + drift + seed);
    float detail = fbm(uv * vec2(25.0, 12.0) + drift * 1.7 + seed * 3.0);
    float eroded = c + broad * 0.16 + detail * 0.06 - 0.18;
    return smoothstep(0.14, 0.72, eroded);
}

float cloudWisps(vec2 uv, float time) {
    float w = sin((uv.x + time * 0.018) * 33.0 + uv.y * 9.0) * 0.5 + 0.5;
    w *= sin((uv.x - time * 0.011) * 17.0 - uv.y * 13.0) * 0.5 + 0.5;
    return smoothstep(0.42, 0.95, w) * 0.12;
}

float cloudLayer(vec2 uv, float time) {
    float c = 0.0;
    float drift = time * 0.018;
    c = max(c, cloudGroup(uv, vec2(fract(0.14 + drift), 0.80), 0.92, time, 0.1));
    c = max(c, cloudGroup(uv, vec2(fract(0.42 + drift * 0.75), 0.66), 0.56, time, 1.1));
    c = max(c, cloudGroup(uv, vec2(fract(0.64 + drift * 0.64), 0.77), 0.74, time, 1.7));
    c = max(c, cloudGroup(uv, vec2(fract(0.87 + drift * 0.55), 0.70), 0.62, time, 3.2));
    c = clamp(c + cloudWisps(uv, time) * c, 0.0, 1.0);
    return clamp(c, 0.0, 1.0);
}

void main() {
    float time = pc.u_cameraTime.w;
    vec3 normal = smooth_wave_normal(v_worldPos.xz, time);
    vec2 rippleUv = v_texCoord * 1.9;
    vec2 ripple = vec2(
        sin(rippleUv.x * 22.0 + time * 1.9) + cos((rippleUv.x + rippleUv.y) * 13.0 + time * 1.3),
        cos(rippleUv.y * 20.0 + time * 1.5) + sin((rippleUv.x - rippleUv.y) * 16.0 + time * 1.7)
    ) * 0.010;
    normal = normalize(normal + vec3(ripple.x, 0.0, ripple.y) + v_baseNormal * 0.02);

    vec3 cameraPos = pc.u_cameraTime.xyz;
    vec3 viewDir = normalize(cameraPos - v_worldPos);
    vec3 lightDir = normalize(vec3(-0.25, 0.88, -0.40));

    float ndv = clamp(dot(normal, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - ndv, 5.0);
    fresnel = clamp(0.04 + fresnel * 0.96, 0.0, 1.0);

    vec3 reflectedView = reflect(-viewDir, normal);
    float horizon = clamp(1.0 - reflectedView.y, 0.0, 1.0);
    vec3 skyReflection = mix(vec3(0.78, 0.91, 1.0), vec3(0.42, 0.68, 0.96), horizon);
    vec2 cloudUv = reflectedView.xz * 0.18 + vec2(0.52, 0.74);
    float clouds = cloudLayer(cloudUv, time) * smoothstep(0.22, 0.74, reflectedView.y);
    skyReflection = mix(skyReflection, vec3(0.97, 0.985, 1.0), clouds * 0.62);
    vec3 deepWater = vec3(0.01, 0.16, 0.26);
    vec3 shallowWater = vec3(0.05, 0.48, 0.58);
    float crest = smoothstep(0.18, 0.34, v_height);
    vec3 refraction = mix(deepWater, shallowWater, 0.42 + v_height * 0.55);
    refraction = mix(refraction, v_color.rgb, 0.18);
    refraction += vec3(0.07, 0.16, 0.14) * crest;

    float diffuse = clamp(dot(normal, lightDir), 0.0, 1.0);
    vec3 halfVector = normalize(lightDir + viewDir);
    float specular = pow(clamp(dot(normal, halfVector), 0.0, 1.0), 88.0);
    float sunGlint = pow(clamp(dot(reflectedView, lightDir), 0.0, 1.0), 180.0);
    float sunSheen = pow(clamp(dot(reflectedView, lightDir), 0.0, 1.0), 38.0);

    vec3 color = mix(refraction, skyReflection, fresnel);
    color += vec3(0.03, 0.08, 0.10) * diffuse;
    color += vec3(0.90, 0.96, 1.0) * specular * (0.36 + fresnel);
    color += vec3(1.0, 0.89, 0.55) * (sunGlint * 2.2 + sunSheen * 0.14);

    outColor = vec4(color, 1.0);
}
