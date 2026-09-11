#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

layout(push_constant) uniform TextPushConstants {
    float screen_width;
    float screen_height;
    float alpha;
    float padding;
} pc;

void main() {
    vec2 ndc = vec2(
        (in_pos.x / max(pc.screen_width, 1.0)) * 2.0 - 1.0,
        (in_pos.y / max(pc.screen_height, 1.0)) * 2.0 - 1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
    out_uv = in_uv;
}
