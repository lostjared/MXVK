#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D text_tex;

void main() {
    out_color = texture(text_tex, out_uv);
}
