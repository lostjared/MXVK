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

float hash(float n) { return fract(sin(n) * 43758.5453123); }
vec2 wrapMirror(vec2 p) { return abs(fract(p) * 2.0 - 1.0); }

void main(void) {
    float t = pc.params.x;
    float aspect = vec2(pc.screenWidth, pc.screenHeight).x / max(vec2(pc.screenWidth, pc.screenHeight).y, 1.0);
    vec2 p = out_uv - 0.5;
    p.x *= aspect;
    vec2 mouseUV = vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    mouseP.x *= aspect;
    p -= mouseP * 0.08;
    float r = length(p);
    float a = atan(p.y, p.x);
    float seg = 9.0 + 3.0 * sin(t * 0.31);
    float stepA = 6.2831853 / seg;
    a = abs(mod(a + t * 0.25, stepA) - stepA * 0.5);
    vec2 q = vec2(cos(a), sin(a)) * r;
    for (int i = 0; i < 4; ++i) {
        q = abs(q * (1.36 + 0.08 * sin(t + float(i)))) - 0.38;
        q = mat2(0.86, -0.51, 0.51, 0.86) * q;
    }
    float band = floor(out_uv.y * 96.0);
    q.x += (hash(band + floor(t * 13.0)) - 0.5) * 0.25;
    vec2 uv = wrapMirror(q / vec2(aspect, 1.0) + 0.5);
    uv += (mouseP - p) * 0.03 * smoothstep(1.35, 0.0, length(p - mouseP));
    vec3 c = texture(sprite_tex, uv).rgb;
    float line = step(0.92, hash(floor(out_uv.y * 180.0) + floor(t * 18.0)));
    c = mix(c, vec3(c.b, c.r, c.g), line * 0.75);
    c += (0.5 + 0.5 * cos(vec3(0.3, 2.7, 5.1) + r * 20.0 - t)) * line * 0.25;
    out_color = vec4(c, 1.0);
}
