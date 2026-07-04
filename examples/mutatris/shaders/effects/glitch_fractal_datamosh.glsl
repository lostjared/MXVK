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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec2 wrapMirror(vec2 p) { return abs(fract(p) * 2.0 - 1.0); }

void main(void) {
    float t = pc.params.x;
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = out_uv - 0.5;
    p.x *= aspect;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.10;
    vec2 z = p;
    float trap = 1.0;
    for (int i = 0; i < 5; ++i) {
        z = abs(z) / max(dot(z, z), 0.18) - vec2(0.78, 0.52 + 0.07 * sin(t));
        trap = min(trap, length(fract(z * 2.0) - 0.5));
    }
    float row = floor(out_uv.y * 72.0);
    float tear = (hash(vec2(row, floor(t * 8.0))) - 0.5) * smoothstep(0.28, 0.02, trap);
    tear += smoothstep(1.15, 0.0, length(p - mouseP)) * 0.25;
    vec2 uv = wrapMirror(out_uv + z * 0.018 + vec2(tear * 0.18, 0.0));
    vec2 off = normalize(z + 1e-5) * (0.006 + 0.04 * smoothstep(0.16, 0.0, trap));
    vec3 c;
    c.r = texture(sprite_tex, wrapMirror(uv + off)).r;
    c.g = texture(sprite_tex, uv).g;
    c.b = texture(sprite_tex, wrapMirror(uv - off)).b;
    vec3 neon = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + trap * 18.0 - t * 3.0);
    out_color = vec4(mix(c, c * neon + neon * 0.18, 0.55), 1.0);
}
