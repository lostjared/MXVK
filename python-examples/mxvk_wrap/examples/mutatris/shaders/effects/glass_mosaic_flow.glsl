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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(17.17, 91.71))) * 43758.5453);
}

vec2 mirrorWrap(vec2 p) {
    return abs(fract(p) * 2.0 - 1.0);
}

void main(void) {
    float t = pc.params.x * 0.6;
    vec2 grid = vec2(18.0, 14.0);
    vec2 g = floor(out_uv * grid);
    vec2 f = fract(out_uv * grid) - 0.5;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseCell = floor(mouseUV * grid);
    float mouseFalloff = smoothstep(2.0, 0.0, distance(g, mouseCell));
    float h = hash(g);
    vec2 drift = vec2(sin(t + h * 6.283185), cos(t * 0.8 + h * 5.0));
    vec2 center = (g + 0.5 + drift * 0.22) / grid;
    center += (mouseUV - center) * mouseFalloff * 0.28;
    float bevel = 1.0 - smoothstep(0.34, 0.5, max(abs(f.x), abs(f.y)));
    vec2 refractUv = out_uv + (center - out_uv) * (0.25 + 0.4 * bevel);
    refractUv += normalize(f + 0.001) * 0.018 * sin(h * 8.0 + t * 3.0);
    vec3 c = texture(sprite_tex, mirrorWrap(refractUv)).rgb;
    vec3 shine = vec3(0.8, 0.95, 1.0) * pow(bevel, 6.0) * (0.35 + h * 0.45);
    out_color = vec4(c * (0.72 + bevel * 0.45) + shine, 1.0);
}
