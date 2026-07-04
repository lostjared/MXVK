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

vec2 mirrorWrap(vec2 p) {
    return abs(fract(p) * 2.0 - 1.0);
}

void main(void) {
    float t = pc.params.x * 0.5;
    vec2 p = out_uv - 0.5;
    p.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    p -= mouseP * 0.10;
    float r = length(p);
    float a = atan(p.y, p.x);
    float radial = abs(sin(r * 38.0 - t * 5.0));
    float spokes = abs(sin(a * 14.0 + sin(r * 9.0 - t) * 2.0));
    float web = pow(1.0 - min(radial, spokes), 5.0);
    vec2 uv = out_uv + normalize(p + 0.0001) * web * 0.075;
    uv += vec2(sin(a * 3.0 + t), cos(a * 4.0 - t)) * 0.012;
    uv += (mouseP - p) * 0.025 * smoothstep(0.9, 0.0, length(p - mouseP));
    vec3 c = texture(sprite_tex, mirrorWrap(uv)).rgb;
    vec3 edge = vec3(1.0, 0.86, 0.58) * web + vec3(0.2, 0.65, 1.0) * pow(spokes, 10.0) * 0.25;
    c = mix(c, vec3(dot(c, vec3(0.333))), 0.35);
    out_color = vec4(c * (0.75 + web * 1.4) + edge, 1.0);
}
