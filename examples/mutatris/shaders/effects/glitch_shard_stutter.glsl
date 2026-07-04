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

float hash(vec2 p) { return fract(sin(dot(p, vec2(53.7, 141.9))) * 43758.5453); }
vec2 wrapMirror(vec2 p) { return abs(fract(p) * 2.0 - 1.0); }

void main(void) {
    float t = pc.params.x;
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = out_uv - 0.5;
    p.x *= aspect;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.08;
    float a = atan(p.y, p.x);
    float r = length(p);
    float shard = floor((a + 3.14159265) / 6.2831853 * 18.0);
    float h = hash(vec2(shard, floor(t * 10.0)));
    a += (h - 0.5) * 0.7;
    r = abs(fract(r * (3.0 + h * 5.0) - t * 0.4) - 0.5);
    vec2 q = vec2(cos(a), sin(a)) * r;
    for (int i = 0; i < 3; ++i) q = abs(q * 1.7) - 0.5;
    vec2 uv = wrapMirror(q / vec2(aspect, 1.0) + 0.5);
    uv += (mouseP - p) * 0.03 * smoothstep(1.25, 0.0, length(p - mouseP));
    vec2 off = vec2(0.018 + h * 0.035, 0.0);
    vec3 c = vec3(texture(sprite_tex, wrapMirror(uv + off)).r, texture(sprite_tex, uv).g, texture(sprite_tex, wrapMirror(uv - off)).b);
    c += (0.5 + 0.5 * cos(vec3(0.4, 2.6, 4.9) + h * 7.0 + t)) * step(0.9, h) * 0.35;
    out_color = vec4(c, 1.0);
}
