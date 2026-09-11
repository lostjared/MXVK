#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D text_tex;

layout(push_constant) uniform TextPushConstants {
    float screen_width;
    float screen_height;
    float alpha;
    float padding;
} pc;

void main() {
    vec4 tex_color = texture(text_tex, out_uv);
    out_color = vec4(tex_color.rgb, tex_color.a * clamp(pc.alpha, 0.0, 1.0));
}
