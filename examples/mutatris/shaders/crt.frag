#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D screen_texture;

layout(push_constant) uniform PushConstants {
    float screen_width;
    float screen_height;
    float sprite_pos_x;
    float sprite_pos_y;
    float sprite_size_w;
    float sprite_size_h;
    float effects_on;
    float padding;
    vec4 params;
} pc;

vec2 curve_uv(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec2 offset = abs(uv.yx) / vec2(pc.params.y);
    uv += uv * offset * offset;
    return uv * 0.5 + 0.5;
}

void main() {
    vec2 curved_uv = curve_uv(out_uv);
    if (any(lessThan(curved_uv, vec2(0.0))) || any(greaterThan(curved_uv, vec2(1.0)))) {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float r = texture(screen_texture, curved_uv + vec2(pc.params.w, 0.0)).r;
    float g = texture(screen_texture, curved_uv).g;
    float b = texture(screen_texture, curved_uv - vec2(pc.params.w, 0.0)).b;
    vec3 color = vec3(r, g, b);
    color -= sin(curved_uv.y * 800.0) * 0.04 * pc.params.z;
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    color += color * luminance * 0.2;
    float vignette = curved_uv.x * curved_uv.y * (1.0 - curved_uv.x) * (1.0 - curved_uv.y);
    color *= clamp(pow(16.0 * vignette, 0.25), 0.0, 1.0);
    out_color = vec4(color, 1.0);
}
