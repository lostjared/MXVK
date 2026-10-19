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

float luminance(vec3 c) {
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main(void) {
    float t = pc.params.x * 0.8;
    vec2 px = 1.0 / max(vec2(pc.screenWidth, pc.screenHeight), vec2(1.0));
    vec2 mouseUV = vec2(0.5);
    vec3 src = texture(sprite_tex, out_uv).rgb;
    float mouseEdge = smoothstep(1.0, 0.0, distance(out_uv, mouseUV));
    float l = luminance(src);
    float gx = luminance(texture(sprite_tex, out_uv + vec2(px.x, 0.0)).rgb) - luminance(texture(sprite_tex, out_uv - vec2(px.x, 0.0)).rgb);
    float gy = luminance(texture(sprite_tex, out_uv + vec2(0.0, px.y)).rgb) - luminance(texture(sprite_tex, out_uv - vec2(0.0, px.y)).rgb);
    float edge = smoothstep(0.04, 0.28, length(vec2(gx, gy)));
    float bands = smoothstep(0.88, 1.0, sin((l + out_uv.y * 0.7) * 55.0 - t * 3.0) * 0.5 + 0.5);
    vec3 neon = 0.55 + 0.45 * cos(vec3(0.0, 2.2, 4.4) + l * 8.0 + t);
    vec3 c = src * 0.42 + neon * (bands * 0.55 + edge * 0.85);
    c += mouseEdge * vec3(0.1, 0.04, 0.14);
    out_color = vec4(c, 1.0);
}
