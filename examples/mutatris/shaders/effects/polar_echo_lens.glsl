#version 450
layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;
layout(binding = 0) uniform sampler2D sprite_tex;

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

vec2 mirrorWrap(vec2 p) {
    return abs(fract(p) * 2.0 - 1.0);
}

void main(void) {
    float t = pc.params.x * 0.45;
    vec2 p = out_uv - 0.5;
    p.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    p -= mouseP * 0.08;
    float r = length(p);
    float a = atan(p.y, p.x);
    float lens = 1.0 / max(0.18, r + 0.08 * sin(a * 5.0 + t));
    vec2 q = vec2(a / 6.283185 + 0.5 + t * 0.05, fract(lens * 0.23 - t * 0.08));
    vec3 acc = vec3(0.0);
    acc += texture(sprite_tex, mirrorWrap(q)).rgb * 0.50;
    acc += texture(sprite_tex, mirrorWrap(q + vec2(0.033, 0.061))).rgb * 0.25;
    acc += texture(sprite_tex, mirrorWrap(q - vec2(0.071, 0.027))).rgb * 0.25;
    acc += texture(sprite_tex, mirrorWrap(q + (mouseP - p) * 0.04)).rgb * smoothstep(1.25, 0.0, length(p - mouseP)) * 0.18;
    float rings = smoothstep(0.96, 1.0, sin(lens * 10.0 + t * 4.0) * 0.5 + 0.5);
    vec3 tint = 0.55 + 0.45 * cos(vec3(0.4, 2.5, 4.6) + a * 2.0 - t);
    out_color = vec4(acc * (0.82 + rings * 0.55) + tint * rings * 0.25, 1.0);
}
