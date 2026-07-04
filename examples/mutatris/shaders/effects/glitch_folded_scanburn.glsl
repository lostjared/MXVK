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

float hash(vec2 p) { return fract(sin(dot(p, vec2(17.17, 91.71))) * 43758.5453); }
vec2 wrapMirror(vec2 p) { return abs(fract(p) * 2.0 - 1.0); }

void main(void) {
    float t = pc.params.x;
    vec2 uv = out_uv;
    vec2 p = uv - 0.5;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    p -= mouseP * 0.08;
    for (int i = 0; i < 6; ++i) {
        p = abs(p * (1.18 + 0.04 * sin(t + float(i)))) - vec2(0.31, 0.27);
        p += 0.025 * sin(p.yx * (8.0 + float(i)) + t);
    }
    float blocks = hash(vec2(floor(out_uv.x * 24.0), floor(out_uv.y * 40.0) + floor(t * 9.0)));
    float burn = smoothstep(0.76, 1.0, blocks) * smoothstep(0.04, 0.0, abs(p.x * p.y));
    uv = wrapMirror(out_uv + p * 0.04 + vec2(burn * 0.12, 0.0));
    uv += (mouseP - p) * 0.025 * smoothstep(1.25, 0.0, length(p - mouseP));
    vec3 c = texture(sprite_tex, uv).rgb;
    float scan = 0.75 + 0.25 * sin(out_uv.y * vec2(pc.screenWidth, pc.screenHeight).y * 3.14159 + burn * 8.0);
    vec3 hot = vec3(1.0, 0.05, 0.65) + vec3(0.0, 0.8, 1.0) * sin(t + p.x * 10.0);
    out_color = vec4(c * scan + hot * burn * 0.45, 1.0);
}
