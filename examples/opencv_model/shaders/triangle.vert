#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

layout(push_constant) uniform SpritePushConstants {
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

void main() {
    vec2 pixel_pos = vec2(pc.spritePosX, pc.spritePosY) + in_pos * vec2(pc.spriteSizeW, pc.spriteSizeH);
    vec2 ndc = vec2(
        (pixel_pos.x / max(pc.screenWidth, 1.0)) * 2.0 - 1.0,
        (pixel_pos.y / max(pc.screenHeight, 1.0)) * 2.0 - 1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    out_uv = vec2(in_uv.x, 1.0 - in_uv.y);
}
