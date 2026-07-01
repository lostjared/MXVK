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

void main() {
    vec4 color = texture(screen_texture, out_uv);
    float strength = clamp(pc.params.y, 0.0, 1.0);
    out_color = vec4(mix(color.rgb, vec3(1.0) - color.rgb, strength), color.a);
}
