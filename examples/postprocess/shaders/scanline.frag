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
    float scanline = 0.86 + 0.14 * sin((out_uv.y * pc.screen_height + pc.params.x * 48.0) * 3.14159);
    float vignette = out_uv.x * out_uv.y * (1.0 - out_uv.x) * (1.0 - out_uv.y);
    float vignette_strength = clamp(pow(16.0 * vignette, 0.22), 0.0, 1.0);
    out_color = vec4(color.rgb * scanline * vignette_strength, color.a);
}
