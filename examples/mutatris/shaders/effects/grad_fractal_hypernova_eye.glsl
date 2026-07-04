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
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.03, 0.39, 0.76) + t));
}

void main(void) {
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.10;
    float r = length(p) + 1e-4;
    float a = atan(p.y, p.x);
    vec2 dir = vec2(cos(a), sin(a));
    float iris = sin(a * 18.0 + log(r) * 4.0 - pc.params.x * 2.2);
    vec2 z = vec2(cos(a + iris * 0.16), sin(a + iris * 0.16)) * (0.28 / r);
    float flare = 0.0;
    for (int i = 0; i < 5; ++i) {
        z = abs(fract(z) * 2.0 - 1.0);
        flare += pow(abs(sin((z.x + z.y) * 6.0 + pc.params.x)), 12.0) * 0.12;
        z *= 1.28;
    }
    z += (mouseP - p) * 0.04 * smoothstep(1.35, 0.0, length(p - mouseP));
    vec2 uv = abs(fract(z / vec2(aspect, 1.0) + 0.5) * 2.0 - 1.0);
    vec3 tex = texture(sprite_tex, uv).rgb;
    vec3 grad = palette(dot(dir, vec2(0.12, 0.08)) + r + flare - pc.params.x * 0.06);
    float core = pow(max(0.0, 1.0 - r * 2.7), 3.0);
    out_color = vec4(mix(tex, grad, 0.75) + grad * (flare + core) * 0.85, 1.0);
}
