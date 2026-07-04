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

vec2 wrapMirror(vec2 p) { return abs(fract(p) * 2.0 - 1.0); }

void main(void) {
    float t = pc.params.x * 0.55;
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 c0 = (out_uv - 0.5) * vec2(2.7 * aspect, 2.7) + vec2(-0.35 + 0.08 * sin(t), 0.05 * cos(t));
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    c0 -= mouseP * 0.12;
    vec2 z = c0;
    float iter = 0.0;
    float trap = 8.0;
    for (int i = 0; i < 22; ++i) {
        z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c0;
        trap = min(trap, abs(z.x * z.y));
        if (dot(z, z) > 9.0) break;
        iter += 1.0;
    }
    float f = iter / 22.0;
    vec2 uv = wrapMirror(out_uv + normalize(z + 1e-5) * (0.01 + f * 0.06));
    vec2 off = vec2(0.02 * sin(trap * 40.0 + t), 0.0);
    uv += (mouseP - c0) * 0.02 * smoothstep(1.0, 0.0, length(c0 - mouseP));
    vec3 tex = vec3(texture(sprite_tex, wrapMirror(uv + off)).r, texture(sprite_tex, uv).g, texture(sprite_tex, wrapMirror(uv - off)).b);
    vec3 acid = 0.55 + 0.45 * cos(vec3(0.0, 2.0, 4.0) + f * 10.0 + trap * 25.0 - t * 4.0);
    out_color = vec4(mix(tex, acid, 0.25 + 0.55 * smoothstep(0.0, 0.05, trap)), 1.0);
}
