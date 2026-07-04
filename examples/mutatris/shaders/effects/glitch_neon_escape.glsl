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
    float t = pc.params.x * 0.4;
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(2.2 * aspect, 2.2);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.08;
    vec2 cst = vec2(-0.72 + 0.08 * sin(t), 0.29 + 0.05 * cos(t * 1.3));
    float it = 0.0;
    for (int i = 0; i < 28; ++i) {
        p = vec2(p.x * p.x - p.y * p.y, 2.0 * p.x * p.y) + cst;
        p += 0.035 * sin(p.yx * 5.0 + t);
        if (dot(p, p) > 8.0) break;
        it += 1.0;
    }
    float n = it / 28.0;
    vec2 uv = wrapMirror(out_uv + p * 0.012 + vec2(sin(n * 20.0 + t) * 0.03, 0.0));
    uv += (mouseP - p) * 0.02 * smoothstep(1.35, 0.0, length(p - mouseP));
    vec3 tex = texture(sprite_tex, uv).rgb;
    vec3 neon = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + n * 14.0 + t * 5.0);
    float contour = pow(abs(sin(n * 40.0)), 8.0);
    out_color = vec4(mix(tex, neon, 0.35 + contour * 0.4) + neon * contour * 0.3, 1.0);
}
