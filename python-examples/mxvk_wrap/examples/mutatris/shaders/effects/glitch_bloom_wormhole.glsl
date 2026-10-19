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

vec3 wormholeColor(float r, float a, float t) {
    vec2 uv = vec2(a / 6.2831853 + 0.5 + t * 0.06, 0.22 / r + t * 0.08);
    for (int i = 0; i < 4; ++i) {
        uv = abs(fract(uv * (1.25 + 0.05 * float(i))) * 2.0 - 1.0);
        uv += sin(uv.yx * 8.0 + t + float(i)) * 0.015;
    }
    float pulse = pow(abs(sin(r * 24.0 - t * 5.0)), 10.0);
    vec3 c = texture(sprite_tex, wrapMirror(uv)).rgb;
    vec3 glow = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + a * 3.0 - t * 2.0);
    c += glow * pulse * (0.25 + smoothstep(0.7, 0.0, r) * 0.55);
    return c;
}

void main(void) {
    float t = pc.params.x;
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.10;
    float r = length(p) + 1e-4;
    float a = atan(p.y, p.x);

    vec3 c = wormholeColor(r, a, t);
    c += vec3(0.1, 0.04, 0.12) * smoothstep(1.25, 0.0, length(p - mouseP)) * 0.25;
    float seam = 1.0 - smoothstep(0.0, 0.18, 3.14159265 - abs(a));
    if (seam > 0.0) {
        float wrappedA = a + (a < 0.0 ? 6.2831853 : -6.2831853);
        c = mix(c, wormholeColor(r, wrappedA, t), seam * 0.5);
    }

    out_color = vec4(c, 1.0);
}
