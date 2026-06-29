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

const float TAU = 6.28318530718;

vec3 aurora(float t) {
    vec3 a = vec3(0.1, 0.4, 0.3);
    vec3 b = vec3(0.3, 0.5, 0.4);
    vec3 c = vec3(1.0, 1.2, 1.5);
    vec3 d = vec3(0.0, 0.15, 0.4);
    return a + b * cos(TAU * (c * t + d));
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
               mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

mat3 rotY(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat3(c, 0, s,
                0, 1, 0,
                -s, 0, c);
}

mat3 rotZ(float a) {
    float s = sin(a);
    float c = cos(a);
    return mat3(c, -s, 0,
                s, c, 0,
                0, 0, 1);
}

void main() {
    float iTime = pc.params.x;
    vec2 iResolution = vec2(pc.screenWidth, pc.screenHeight);

    float aspect = iResolution.x / iResolution.y;
    vec2 p = (out_uv - 0.5) * vec2(aspect, 1.0);

    vec3 v = vec3(p, 1.0);
    mat3 R = rotZ(iTime * 0.3) * rotY(sin(iTime * 0.5) * 0.2);
    vec3 r = R * v;

    float persp = 0.8;
    float zf = 1.0 / (1.0 + r.z * persp);
    vec2 q = r.xy * zf;

    float rad = length(q) + 1e-6;
    float ang = atan(q.y, q.x);
    float base = 2.5;
    float period = log(base);
    float t = iTime * 0.5;
    float k = fract((log(rad) - t) / period);
    float rw = exp(k * period);
    vec2 qwrap = vec2(cos(ang + t * 0.2), sin(ang + t * 0.2)) * rw;

    float N = 6.0;
    float stepA = TAU / N;
    float a = mod(atan(qwrap.y, qwrap.x), stepA);
    a = abs(a - stepA * 0.5);
    vec2 kaleido = vec2(cos(a), sin(a)) * length(qwrap);
    kaleido.x /= aspect;
    vec2 sampUV = fract(kaleido + 0.5);

    vec3 col = texture(sprite_tex, sampUV).rgb;

    vec3 auroraGlow = vec3(0.0);
    vec2 centered = out_uv - 0.5;
    for (float i = 0.0; i < 5.0; i++) {
        float yOff = i * 0.04 + noise(vec2(centered.x * 6.0 + i, iTime * 0.3 + i)) * 0.06;
        float streak = exp(-pow((centered.y - yOff - 0.05) * 9.0, 2.0));
        auroraGlow += aurora(i * 0.18 + iTime * 0.15) * streak;
    }

    col += auroraGlow * 0.45;

    float chroma = 0.01 + 0.005 * sin(iTime * 2.0);
    col.r = mix(col.r, texture(sprite_tex, sampUV + vec2(chroma, 0.0)).r, 0.5);
    col.b = mix(col.b, texture(sprite_tex, sampUV - vec2(chroma, 0.0)).b, 0.5);

    out_color = vec4(col, 1.0) * pc.params.w;
}
