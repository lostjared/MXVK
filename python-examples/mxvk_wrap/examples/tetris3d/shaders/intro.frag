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

const float PI = 3.1415926535897932384626433832795;

float pingPong(float x, float lengthVal) {
    float m = mod(x, lengthVal * 2.0);
    return m <= lengthVal ? m : lengthVal * 2.0 - m;
}

void main(void) {
    vec2 tc = out_uv;
    float time_f = pc.params.x;
    float fade = clamp(pc.params.w, 0.0, 1.0);

    vec2 uv = tc * 2.0 - 1.0;
    float len = length(uv);
    float time_t = pingPong(time_f, 10.0);
    float bubble = smoothstep(0.8, 1.0, 1.0 - len);
    bubble = sin(bubble * time_t);

    vec2 distort = uv * (1.0 + 0.1 * sin(time_f + len * 20.0));
    distort = sin(distort * time_t);

    vec2 sample_uv = clamp(distort * 0.5 + 0.5, vec2(0.0), vec2(1.0));
    vec4 texColor = texture(sprite_tex, sample_uv);

    vec3 whiteMix = mix(texColor.rgb, vec3(1.0), clamp(bubble, 0.0, 1.0));
    float alpha = texColor.a * fade;
    out_color = vec4(mix(texColor.rgb, whiteMix, 0.75) * fade, alpha);
}
