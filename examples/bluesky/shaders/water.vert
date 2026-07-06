#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_texCoord;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec4 a_color;

layout(push_constant) uniform PushConstants {
    mat4 u_viewProjection;
    vec4 u_cameraTime;
} pc;

layout(location = 0) out vec3 v_worldPos;
layout(location = 1) out vec2 v_texCoord;
layout(location = 2) out vec3 v_baseNormal;
layout(location = 3) out float v_height;
layout(location = 4) out vec4 v_color;

float value_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = fract(sin(dot(i, vec2(127.1, 311.7))) * 43758.5453);
    float b = fract(sin(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);
    float d = fract(sin(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7))) * 43758.5453);

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float region_variation(vec2 p) {
    float a = value_noise(p * 0.032);
    float b = value_noise(p * 0.071 + vec2(19.3, -8.7));
    float c = value_noise(p * 0.015 + vec2(-3.2, 11.6));
    return clamp(a * 0.48 + b * 0.32 + c * 0.20, 0.0, 1.0);
}

float area_variation(vec2 p) {
    float a = sin(dot(p, vec2(0.073, 0.041)));
    float b = cos(dot(p, vec2(-0.037, 0.089)));
    float c = sin(dot(p, vec2(0.019, -0.061)) + a * 1.7);
    return 0.78 + 0.28 * (a * 0.45 + b * 0.35 + c * 0.20);
}

void apply_gerstner_wave(
    vec2 base,
    vec2 direction,
    float amplitude,
    float wavelength,
    float speed,
    float steepness,
    float time,
    inout vec3 displacement,
    inout vec3 tangent_x,
    inout vec3 tangent_z) {
    vec2 dir = normalize(direction);
    float k = 6.2831853 / wavelength;
    float phase = k * dot(dir, base) + speed * time;
    float s = sin(phase);
    float c = cos(phase);
    float horizontal = steepness * amplitude;

    displacement.x += dir.x * horizontal * c;
    displacement.y += amplitude * s;
    displacement.z += dir.y * horizontal * c;

    float slope_term = horizontal * k * s;
    tangent_x += vec3(
        -dir.x * dir.x * slope_term,
        amplitude * k * dir.x * c,
        -dir.y * dir.x * slope_term);
    tangent_z += vec3(
        -dir.x * dir.y * slope_term,
        amplitude * k * dir.y * c,
        -dir.y * dir.y * slope_term);
}

void main() {
    float time = pc.u_cameraTime.w;
    vec2 base = a_position.xz;
    float local = area_variation(base);
    float region = region_variation(base);
    float energetic = mix(0.72, 1.28, region);
    float short_wave_mix = value_noise(base * 0.055 + vec2(4.0, 13.0));
    float phase_offset = (region - 0.5) * 5.0;
    vec2 direction_warp = vec2(region - 0.5, short_wave_mix - 0.5) * 0.42;

    vec3 displacement = vec3(0.0);
    vec3 tangent_x = vec3(1.0, 0.0, 0.0);
    vec3 tangent_z = vec3(0.0, 0.0, 1.0);

    apply_gerstner_wave(base, vec2(1.0, 0.28) + direction_warp, 0.42 * local * energetic, 8.2 + region * 2.1, 1.05, 0.48, time + phase_offset, displacement, tangent_x, tangent_z);
    apply_gerstner_wave(base, vec2(-0.42, 1.0) - direction_warp.yx, 0.28 * mix(0.82, 1.18, short_wave_mix), 5.0 + region * 1.4, 1.38, 0.42, time - phase_offset * 0.4, displacement, tangent_x, tangent_z);
    apply_gerstner_wave(base, vec2(0.75, -0.62) + direction_warp * 0.6, 0.17 * local * mix(0.75, 1.35, short_wave_mix), 3.0 + region * 0.9, 1.88, 0.36, time + phase_offset * 0.7, displacement, tangent_x, tangent_z);
    apply_gerstner_wave(base, vec2(-0.88, -0.24) - direction_warp * 0.4, 0.10 * energetic, 2.0 + short_wave_mix * 0.7, 2.32, 0.25, time, displacement, tangent_x, tangent_z);
    apply_gerstner_wave(base, vec2(0.16, 1.0) + direction_warp.yx, 0.05 + 0.05 * short_wave_mix, 1.35 + region * 0.35, 2.85, 0.18, time - phase_offset, displacement, tangent_x, tangent_z);

    vec3 pos = a_position + displacement;
    vec3 wave_normal = normalize(cross(tangent_z, tangent_x));

    v_worldPos = pos;
    v_texCoord = a_texCoord;
    v_baseNormal = mix(a_normal, wave_normal, 0.98);
    v_height = pos.y;
    v_color = vec4(mix(a_color.rgb * 0.88, a_color.rgb * 1.13, region), a_color.a);

    gl_Position = pc.u_viewProjection * vec4(pos, 1.0);
}
