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

mat2 rot(float a) {
    float s = sin(a), c = cos(a);
    return mat2(c, -s, s, c);
}

vec3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.05, 0.23, 0.48) + t));
}

void main(void) {
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0) * 2.0;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.08;
    vec2 z = p;
    float glow = 0.0;
    for (int i = 0; i < 7; ++i) {
        z = abs(z) / max(dot(z, z), 0.22) - vec2(0.82, 0.48);
        z = rot(0.24 + 0.08 * sin(pc.params.x + float(i))) * z;
        glow += exp(-8.0 * abs(length(z) - 0.62)) * 0.12;
    }
    float r = length(p);
    float corona = pow(max(0.0, 1.0 - r), 1.8) + glow;
    vec3 tex = texture(sprite_tex, abs(fract(out_uv + z * 0.018) * 2.0 - 1.0)).rgb;
    tex += vec3((mouseP - p) * 0.01 * smoothstep(1.35, 0.0, length(p - mouseP)), 0.0);
    vec3 grad = palette(glow + r * 0.35 - pc.params.x * 0.08) * vec3(1.35, 0.9, 0.55);
    out_color = vec4(mix(tex, grad, 0.72) + grad * corona * 0.65, 1.0);
}
