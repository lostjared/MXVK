#version 450

layout(push_constant) uniform PushConstants {
    mat4 u_viewProjection;
    vec4 u_cameraTime;
    vec4 u_viewport;
} pc;

layout(location = 0) in vec3 v_worldPos;
layout(location = 1) in vec2 v_texCoord;
layout(location = 2) in vec3 v_baseNormal;
layout(location = 3) in float v_height;
layout(location = 4) in vec4 v_color;

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

float filtered_highlight(float value, float edge, float softness) {
    float width = max(fwidth(value), softness);
    return smoothstep(edge - width * 3.0, edge + width * 2.0, value);
}

float smooth_region(vec2 p) {
    float a = sin(dot(p, vec2(0.031, 0.047)));
    float b = cos(dot(p, vec2(-0.022, 0.064)));
    float c = sin(dot(p, vec2(0.011, -0.018)) + a * 0.7);
    return (a * 0.42 + b * 0.34 + c * 0.24) * 0.5 + 0.5;
}

vec3 micro_normal(vec2 p, float time, float strength) {
    float region = smooth_region(p);
    vec2 drift_a = vec2(time * mix(0.34, 0.52, region), -time * mix(0.14, 0.25, region));
    vec2 drift_b = vec2(-time * mix(0.20, 0.34, region), time * mix(0.24, 0.39, region));
    vec2 p1 = p * mix(4.8, 7.2, region) + drift_a;
    vec2 p2 = p * mix(8.0, 12.4, 1.0 - region) + drift_b;
    float x = sin(dot(p1, vec2(1.0, 0.36))) + cos(dot(p2, vec2(-0.42, 1.0)));
    float z = cos(dot(p1, vec2(0.18, 1.0))) + sin(dot(p2, vec2(1.0, -0.22)));
    return vec3(x, 0.0, z) * strength * mix(0.70, 1.35, region);
}

void main() {
    float time = pc.u_cameraTime.w;
    vec3 cameraPos = pc.u_cameraTime.xyz;
    float cameraDistance = length(cameraPos - v_worldPos);
    float nearDetail = 1.0 - smoothstep(42.0, 82.0, cameraDistance);
    float region = smooth_region(v_worldPos.xz);

    vec3 normal = normalize(v_baseNormal + micro_normal(v_worldPos.xz, time, 0.030 * nearDetail));

    vec3 viewDir = normalize(cameraPos - v_worldPos);
    vec3 lightDir = normalize(vec3(-0.25, 0.88, -0.40));

    float ndv = clamp(dot(normal, viewDir), 0.0, 1.0);
    float fresnel = pow(1.0 - ndv, 5.0);
    fresnel = clamp(0.04 + fresnel * 0.96, 0.0, 1.0);

    vec3 reflectedView = reflect(-viewDir, normal);
    float horizon = clamp(1.0 - reflectedView.y, 0.0, 1.0);
    vec3 skyReflection = mix(vec3(0.78, 0.91, 1.0), vec3(0.42, 0.68, 0.96), horizon);
    vec3 deepWater = mix(vec3(0.004, 0.085, 0.16), vec3(0.010, 0.13, 0.22), region);
    vec3 midWater = mix(vec3(0.018, 0.28, 0.42), vec3(0.030, 0.37, 0.50), region);
    vec3 shallowWater = mix(vec3(0.055, 0.50, 0.60), vec3(0.085, 0.64, 0.70), region);
    float crest = smoothstep(0.24, 0.58, v_height);
    float depthView = smoothstep(0.12, 0.86, ndv);
    vec3 refraction = mix(mix(deepWater, midWater, depthView), shallowWater, crest * 0.65);
    refraction = mix(refraction, v_color.rgb, 0.18);
    refraction += vec3(0.08, 0.18, 0.15) * crest;

    float diffuse = clamp(dot(normal, lightDir), 0.0, 1.0);
    vec3 halfVector = normalize(lightDir + viewDir);
    float specularDot = clamp(dot(normal, halfVector), 0.0, 1.0);
    float sunDot = clamp(dot(reflectedView, lightDir), 0.0, 1.0);
    float sunVisibility = nearDetail * smoothstep(0.34, 0.64, ndv);
    float peakMask = smoothstep(0.08, 0.58, v_height);
    float specular = filtered_highlight(specularDot, 0.955, 0.010) * pow(specularDot, 12.0) * sunVisibility;
    float sunSheen = filtered_highlight(sunDot, 0.925, 0.010) * pow(sunDot, 5.0) * sunVisibility;

    vec3 color = mix(refraction, skyReflection, fresnel);
    color += vec3(0.03, 0.08, 0.10) * diffuse;
    color += vec3(0.88, 0.96, 1.0) * specular * (0.050 + fresnel * 0.10) * (0.35 + peakMask * 0.65);
    color += vec3(1.0, 0.92, 0.68) * sunSheen * 0.065;

    outColor = vec4(color, 1.0);
}
