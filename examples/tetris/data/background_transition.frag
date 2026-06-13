#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D sprite_tex;

layout(push_constant) uniform PushConstants {
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

void main(void) {
    vec2 tc = out_uv;
    float time_f = pc.params.x;

    float speed = 5.0;
    float amplitude = 0.03;
    float wavelength = 10.0;
    float duration = 2.5;
    float cycle = clamp(time_f / duration, 0.0, 1.0);
    float fade = sin(cycle * 3.1415926535897932384626433832795);

    float ripple = sin(tc.x * wavelength + time_f * speed) * amplitude;
    ripple += sin(tc.y * wavelength + time_f * speed) * amplitude;
    vec2 rippleTC = tc + vec2(ripple, ripple);

    vec4 originalColor = texture(sprite_tex, tc);
    vec4 rippleColor = texture(sprite_tex, rippleTC);
    out_color = mix(originalColor, rippleColor, fade);
}
