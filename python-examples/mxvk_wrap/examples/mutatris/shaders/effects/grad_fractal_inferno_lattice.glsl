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

float hash(vec2 p) { return fract(sin(dot(p, vec2(41.0, 289.0))) * 43758.5453); }
vec3 palette(float t) {
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.0, 0.09, 0.21) + t));
}

void main(void) {
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0) * 2.0;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.08;
    vec2 z = p;
    float line = 0.0;
    for (int i = 0; i < 6; ++i) {
        z = abs(z * (1.28 + 0.04 * sin(pc.params.x + float(i)))) - vec2(0.44, 0.36);
        line += smoothstep(0.03, 0.0, abs(sin(z.x * 8.0) + sin(z.y * 8.0)) * 0.5);
    }
    float block = hash(floor(out_uv * vec2(32.0, 20.0)) + floor(pc.params.x * 4.0));
    vec2 uv = abs(fract(out_uv + z * 0.025 + vec2(block * line * 0.015, 0.0)) * 2.0 - 1.0);
    uv += (mouseP - p) * 0.02 * smoothstep(1.35, 0.0, length(p - mouseP));
    vec3 tex = texture(sprite_tex, uv).rgb;
    vec3 grad = palette(length(z) * 0.2 + line * 0.12 - pc.params.x * 0.08) * vec3(1.45, 0.85, 0.38);
    out_color = vec4(mix(tex * 0.45, grad, 0.8) + grad * line * 0.18, 1.0);
}
