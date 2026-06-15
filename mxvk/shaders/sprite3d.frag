#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 1) in vec4 out_color;
layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform sampler2D sprite_tex;

layout(push_constant) uniform Sprite3DPushConstants {
    vec4 position_size_x;
    vec4 color;
    vec4 size_y_rotation_alpha;
} pc;

void main() {
    vec4 tex = texture(sprite_tex, out_uv);
    float alpha = tex.a * out_color.a;
    if (alpha < pc.size_y_rotation_alpha.z) {
        discard;
    }
    frag_color = vec4(tex.rgb * out_color.rgb, alpha);
}
