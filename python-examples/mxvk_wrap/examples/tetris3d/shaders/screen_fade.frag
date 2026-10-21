#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D sprite_tex;

layout(push_constant) uniform PushConstants {
    float screenWidth;
    float screenHeight;
    float spritePosX;
    float spritePosY;
    float spriteSizeW;
    float spriteSizeH;
    float effectsOn;
    float padding2;
    vec4 params;
} pc;

void main(void) {
    vec4 tex_color = texture(sprite_tex, out_uv);
    float alpha = clamp(pc.params.w, 0.0, 1.0);
    out_color = vec4(tex_color.rgb, tex_color.a * alpha);
}
