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

void main() {
    float time = pc.u_cameraTime.w;
    vec3 pos = a_position;
    pos.y += combined_height(pos.xz, time);

    v_worldPos = pos;
    v_texCoord = a_texCoord;
    v_baseNormal = a_normal;
    v_height = pos.y;
    v_color = a_color;

    gl_Position = pc.u_viewProjection * vec4(pos, 1.0);
}
