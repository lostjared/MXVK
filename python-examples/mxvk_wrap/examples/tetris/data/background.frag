#version 450

layout(set = 0, binding = 1, std140) uniform SpriteExtended {
    vec4 mouse;
    vec4 u0;
    vec4 u1;
    vec4 u2;
    vec4 u3;
    vec4 custom_uniforms[16];
    vec4 audio_bands;
    vec4 audio_history;
} ext;

#define iMouse ext.mouse
#define iResolution ext.u0.zw
#define time_f ext.u2.y

layout(location = 0) out vec4 color;
layout(location = 0) in vec2 tc;
layout(set = 0, binding = 0) uniform sampler2D samp;

mat3 rotX(float angle) {
    float sine = sin(angle), cosine = cos(angle);
    return mat3(1, 0, 0, 0, cosine, -sine, 0, sine, cosine);
}

mat3 rotY(float angle) {
    float sine = sin(angle), cosine = cos(angle);
    return mat3(cosine, 0, sine, 0, 1, 0, -sine, 0, cosine);
}

mat3 rotZ(float angle) {
    float sine = sin(angle), cosine = cos(angle);
    return mat3(cosine, -sine, 0, sine, cosine, 0, 0, 0, 1);
}

void main() {
    float aspect = iResolution.x / iResolution.y;
    vec2 aspect_ratio = vec2(aspect, 1.0);
    vec2 mouse = (iMouse.z > 0.5) ? (iMouse.xy / iResolution) : vec2(0.5);
    vec2 position_2d = (tc - mouse) * aspect_ratio;
    float angle_x = 0.25 * sin(time_f * 0.7);
    float angle_y = 0.25 * cos(time_f * 0.6);
    float angle_z = time_f * 0.5;
    vec3 position_3d = vec3(position_2d, 1.0);
    vec3 rotated = rotZ(angle_z) * rotY(angle_y) * rotX(angle_x) * position_3d;
    vec2 projected = rotated.xy / (1.0 + rotated.z * 0.6);
    float distance_from_center = length(position_2d);
    projected *= 1.0 + 0.2 * sin(distance_from_center * 15.0 - time_f * 2.0);
    float period = log(1.7);
    float radius = length(projected) + 1e-6;
    float angle = atan(projected.y, projected.x) + time_f * 0.35 * 6.2831853;
    float wrapped_radius = exp(fract((log(radius) - time_f * 0.45) / period) * period);
    vec2 wrapped = vec2(cos(angle), sin(angle)) * wrapped_radius;
    color = texture(samp, fract(wrapped / aspect_ratio + mouse));
}
