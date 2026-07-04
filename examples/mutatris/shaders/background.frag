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

float pingPong(float x, float lengthValue) {
    float modVal = mod(x, lengthValue * 2.0);
    return modVal <= lengthValue ? modVal : lengthValue * 2.0 - modVal;
}

void main(void) {
    vec2 tc = out_uv;
    float time_f = pc.params.x;
    vec2 iResolution = vec2(pc.screenWidth, pc.screenHeight);
    vec2 uv = tc * 2.0 - 1.0;
    uv.x *= iResolution.x / max(iResolution.y, 1.0);

    float len = length(uv);
    float time_t = pingPong(time_f, 10.0);
    float bubble = smoothstep(0.8, 1.0, 1.0 - len);
    bubble = sin(bubble * time_t);

    vec2 distort = uv * (1.0 + 0.1 * sin(time_f + len * 20.0));
    distort = sin(distort * time_t);

    vec2 sampleUv = distort * 0.5 + 0.5;
    vec4 texColor = texture(sprite_tex, sampleUv);
    out_color = mix(texColor, vec4(1.0, 1.0, 1.0, 1.0), clamp(bubble, 0.0, 1.0));
}
