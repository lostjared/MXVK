#version 450

layout(push_constant) uniform PushConstants {
    mat4 u_viewProjection;
    vec4 u_cameraTime;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 outColor;

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

float smoothCloudDetail(vec2 uv, float time, float seed) {
    vec2 p = uv + vec2(time * 0.010, time * 0.002) + seed;
    float a = sin(dot(p, vec2(19.7, 8.3)));
    float b = sin(dot(p + a * 0.025, vec2(-13.1, 17.9)));
    float c = cos(dot(p + b * 0.020, vec2(27.4, -6.6)));
    return (a * 0.45 + b * 0.35 + c * 0.20) * 0.5 + 0.5;
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

    float broad = smoothCloudDetail(uv * vec2(1.8, 1.1), time, seed);
    float detail = smoothCloudDetail(uv * vec2(4.2, 2.4) + vec2(0.07, -0.03), time * 1.3, seed * 2.1);
    float eroded = c + broad * 0.10 + detail * 0.045 - 0.17;
    return smoothstep(0.12, 0.70, eroded);
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
    c *= smoothstep(0.58, 0.70, uv.y);
    c *= 1.0 - smoothstep(0.94, 1.0, uv.y);
    return clamp(c, 0.0, 1.0);
}

void main() {
    float time = pc.u_cameraTime.w;
    vec2 skyUv = vec2(v_uv.x, 1.0 - v_uv.y);
    vec3 horizon = vec3(0.62, 0.84, 1.0);
    vec3 zenith = vec3(0.04, 0.25, 0.70);
    vec3 sky = mix(horizon, zenith, pow(smoothstep(0.0, 1.0, skyUv.y), 0.75));

    float clouds = cloudLayer(skyUv, time);
    float cloudEdge = smoothstep(0.10, 0.35, clouds) * (1.0 - smoothstep(0.72, 0.98, clouds));
    float cloudCore = smoothstep(0.45, 1.0, clouds);
    vec3 cloudShadow = vec3(0.68, 0.76, 0.88);
    vec3 cloudLight = vec3(0.98, 0.985, 0.965);
    vec3 cloudColor = mix(cloudShadow, cloudLight, smoothstep(0.18, 0.78, clouds));
    cloudColor += vec3(1.0, 0.88, 0.58) * cloudEdge * 0.12;
    cloudColor = mix(cloudColor, vec3(0.90, 0.93, 0.98), cloudCore * 0.12);
    sky = mix(sky, cloudColor, clouds * 0.82);

    vec2 sunUv = vec2(0.78, 0.73);
    vec2 sunDelta = (skyUv - sunUv) * vec2(1.7778, 1.0);
    float sunDistance = length(sunDelta);
    float core = exp(-sunDistance * sunDistance * 980.0);
    float innerGlow = exp(-sunDistance * sunDistance * 95.0);
    float outerGlow = exp(-sunDistance * sunDistance * 18.0);
    float rayPattern = pow(max(0.0, dot(normalize(sunDelta + vec2(0.0001)), normalize(vec2(0.85, 0.28)))), 18.0);
    float rays = rayPattern * smoothstep(0.36, 0.04, sunDistance) * 0.10;
    sky += vec3(1.0, 0.87, 0.48) * outerGlow * 0.24;
    sky += vec3(1.0, 0.93, 0.70) * innerGlow * 0.58;
    sky += vec3(1.0, 0.96, 0.72) * rays;
    sky = mix(sky, vec3(1.0, 0.985, 0.88), clamp(core * 1.4, 0.0, 1.0));
    sky = min(sky, vec3(1.0));

    outColor = vec4(sky, 1.0);
}
