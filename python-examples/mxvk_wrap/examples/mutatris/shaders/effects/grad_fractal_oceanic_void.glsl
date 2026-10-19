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
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.48, 0.66, 0.9) + t));
}

void main(void) {
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0) * 2.0;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.08;
    vec2 z = p;
    float foam = 0.0;
    for (int i = 0; i < 6; ++i) {
        z = abs(z) / max(dot(z, z), 0.16) - vec2(0.58 + 0.05 * sin(pc.params.x), 0.7);
        z += 0.035 * sin(z.yx * 6.0 + pc.params.x + float(i));
        foam += exp(-12.0 * abs(length(z) - 0.72)) * 0.12;
    }
    vec2 uv = abs(fract(out_uv + z * 0.015 + vec2(sin(z.y * 5.0) * 0.02, 0.0)) * 2.0 - 1.0);
    uv += (mouseP - p) * 0.02 * smoothstep(1.35, 0.0, length(p - mouseP));
    vec3 tex = texture(sprite_tex, uv).rgb;
    vec3 grad = palette(foam + length(p) * 0.25 + pc.params.x * 0.035) * vec3(0.45, 1.1, 1.45);
    out_color = vec4(mix(tex * 0.5, grad, 0.76) + grad * foam * 0.6, 1.0);
}
