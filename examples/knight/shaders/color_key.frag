#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D sprite_tex;

void main() {
    vec4 tex_color = texture(sprite_tex, out_uv);
    if (all(greaterThan(tex_color.rgb, vec3(0.95)))) {
        discard;
    }
    out_color = tex_color;
}
