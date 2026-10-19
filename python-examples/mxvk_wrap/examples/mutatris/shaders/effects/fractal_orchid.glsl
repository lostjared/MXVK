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

mat2 rot(float a) {
    float s = sin(a), c = cos(a);
    return mat2(c, -s, s, c);
}

void main(void) {
    float t = pc.params.x * 0.42;
    vec2 p = (out_uv - 0.5) * 2.0;
    p.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    p -= mouseP * 0.08;
    vec2 z = p;
    float trap = 10.0;
    for (int i = 0; i < 9; ++i) {
        z = abs(z) / max(dot(z, z), 0.18) - vec2(0.72 + 0.08 * sin(t), 0.48);
        z *= rot(0.35 + sin(t + float(i)) * 0.18);
        trap = min(trap, abs(z.x) + abs(z.y));
    }
    vec2 uv = mirrorWrap(out_uv + z * 0.025);
    uv += (mouseP - p) * 0.02 * exp(-length(p - mouseP) * 2.5);
    vec3 src = texture(sprite_tex, uv).rgb;
    float petals = exp(-trap * 3.2);
    vec3 orchid = 0.5 + 0.5 * cos(vec3(0.7, 2.0, 4.8) + petals * 5.0 + t * 2.0);
    vec3 c = mix(src * 0.65, src * orchid + orchid * 0.35, petals);
    out_color = vec4(c, 1.0);
}
