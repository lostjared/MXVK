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

vec3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.32, 0.56, 0.82) + t));
}

void main(void) {
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.10;
    float r = length(p);
    float a = atan(p.y, p.x);
    vec2 dir = vec2(cos(a), sin(a));
    float petal = abs(sin(a * 8.0 + sin(r * 14.0 - pc.params.x) * 1.5));
    vec2 z = dir * (r + petal * 0.12);
    for (int i = 0; i < 5; ++i) {
        z = abs(z * 1.52) - 0.48;
        z += sin(z.yx * 4.0 + pc.params.x) * 0.025;
    }
    z += (mouseP - p) * 0.04 * smoothstep(1.35, 0.0, length(p - mouseP));
    float veil = pow(1.0 - petal, 3.0) + pow(max(0.0, 1.0 - r * 1.5), 2.0);
    vec2 uv = abs(fract(out_uv + z * 0.035) * 2.0 - 1.0);
    vec3 tex = texture(sprite_tex, uv).rgb;
    vec3 grad = palette(dot(dir, vec2(0.16, 0.09)) + r * 0.7 + pc.params.x * 0.04);
    out_color = vec4(mix(tex, grad, 0.7) + grad * veil * 0.55, 1.0);
}
