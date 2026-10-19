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
    return 0.5 + 0.5 * cos(6.28318 * (vec3(0.62, 0.34, 0.08) + t));
}

void main(void) {
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.09;
    float r = length(p) + 0.0001;
    float a = atan(p.y, p.x);
    vec2 dir = vec2(cos(a), sin(a));
    float fold = fract((log(r) * 1.7 - pc.params.x * 0.45) / log(1.68));
    a += sin(fold * 14.0 + pc.params.x * 1.7) * 0.42;
    vec2 q = dir * exp(fold * log(1.68));
    for (int i = 0; i < 4; ++i) {
        q = abs(q * 1.45) - vec2(0.55, 0.38);
    }
    vec2 uv = abs(fract(q / vec2(aspect, 1.0) + 0.5) * 2.0 - 1.0);
    uv += (mouseP - p) * 0.03 * smoothstep(1.35, 0.0, length(p - mouseP));
    vec3 tex = texture(sprite_tex, uv).rgb;
    float gate = pow(abs(sin(fold * 18.0 + pc.params.x * 2.0)), 7.0);
    vec3 grad = palette(fold + dot(dir, vec2(0.11, 0.07)) + length(q) * 0.1);
    out_color = vec4(mix(tex * 0.55, grad, 0.78) + grad * gate * 0.7, 1.0);
}
