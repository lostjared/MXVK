#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D sprite_tex;

void main() {
    vec4 tex_color = texture(sprite_tex, out_uv);
    // The knight artwork uses a magenta color key. Keep white highlights on
    // the piece while discarding only pixels with strong red and blue and a
    // low green component.
    if (tex_color.r > 0.8 && tex_color.g < 0.25 && tex_color.b > 0.8) {
        discard;
    }
    out_color = tex_color;
}
