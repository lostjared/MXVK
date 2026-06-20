#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragViewPos;
layout(location = 3) in vec3 fragLocalPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

const float iAmplitude = 1.0;
const float iFrequency = 1.0;
const float iBrightness = 1.2;
const float iContrast = 1.1;
const float iSaturation = 1.2;
const float iHueShift = 0.0;
const float iZoom = 1.0;
const float iRotation = 0.0;

vec3 adjustBrightness(vec3 col, float b) {
    return col * b;
}

vec3 adjustContrast(vec3 col, float c) {
    return (col - 0.5) * c + 0.5;
}

vec3 adjustSaturation(vec3 col, float s) {
    float gray = dot(col, vec3(0.299, 0.587, 0.114));
    return mix(vec3(gray), col, s);
}

vec3 palette(float t) {
    vec3 a = vec3(0.5);
    vec3 b = vec3(0.5);
    vec3 c = vec3(1.0);
    vec3 d = vec3(0.00, 0.33, 0.67);
    return a + b * cos(6.28318 * (c * t + d + iHueShift));
}

vec2 wrapUV(vec2 uv) {
    return 1.0 - abs(1.0 - 2.0 * fract(uv * 0.5));
}

vec4 sampleWrappedUV(vec2 uv) {
    vec2 ts = vec2(textureSize(texSampler, 0));
    vec2 eps = 0.5 / ts;
    vec2 wrapped = wrapUV(uv);
    vec2 sampleUv = clamp(wrapped, eps, 1.0 - eps);
    return texture(texSampler, sampleUv);
}

vec2 rotate2D(vec2 p, float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c) * p;
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash(i + vec2(0.0, 0.0));
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = rot * p * 2.0 + vec2(2.0);
        a *= 0.5;
    }
    return v;
}

vec3 sampleLiquid(vec2 uv, float t, float strength) {
    vec2 p = uv - vec2(0.5);
    p += fragLocalPos.xy * 0.08;
    p = rotate2D(p, iRotation);
    p /= max(iZoom, 0.1);

    vec2 q = vec2(0.0);
    q.x = fbm(p + vec2(0.0, 0.0) + 0.05 * t);
    q.y = fbm(p + vec2(5.2, 1.3) + 0.05 * t);

    vec2 r = vec2(0.0);
    r.x = fbm(p + 4.0 * q + vec2(t * 0.15));
    r.y = fbm(p + 4.0 * q + vec2(t * 0.05, 2.8));

    float f = fbm(p + 4.0 * r);

    vec2 fluidUV = uv + (r * 0.12 * strength * iAmplitude);
    vec3 texCol = sampleWrappedUV(fluidUV).rgb;

    vec3 rainbow = palette(length(q) + f + t * 0.2);
    vec3 col = mix(texCol, texCol * rainbow * 1.45, 0.58 * iSaturation);

    float shine = f * f * f * 1.5 * iContrast;
    col += shine * rainbow * strength;
    col *= 0.8 + 0.5 * f;

    return col;
}

void main() {
    float time_f = ubo.fx.x;
    float fade = clamp(ubo.fx.w, 0.0, 1.0);

    vec2 uv = wrapUV(fragTexCoord);
    vec4 base = sampleWrappedUV(uv);
    if (base.a <= 0.0001) {
        base = vec4(0.96, 0.97, 1.0, 1.0);
    }

    float t = time_f * (0.1 + iFrequency * 0.1);
    float strength = 1.0 + (clamp(iAmplitude, 0.0, 2.0) * 0.5);

    vec2 offset = vec2(0.005 * strength, 0.0);
    vec3 col;
    col.r = sampleLiquid(uv + offset, t, strength).r;
    col.g = sampleLiquid(uv, t, strength).g;
    col.b = sampleLiquid(uv - offset, t, strength).b;

    col = adjustBrightness(col, iBrightness);
    col = adjustContrast(col, iContrast);
    col = adjustSaturation(col, iSaturation);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(-fragViewPos);
    vec3 L = normalize(vec3(0.0, 10.0, 6.0) - fragViewPos);
    vec3 H = normalize(L + V);

    float ambient = 0.42;
    float diffuse = max(dot(N, L), 0.0);
    float specular = pow(max(dot(N, H), 0.0), 42.0);
    float rim = pow(1.0 - max(dot(N, V), 0.0), 2.0) * 0.14;
    float fill = 0.12 * max(dot(N, vec3(0.0, 0.0, 1.0)), 0.0);

    float lighting = clamp(ambient + fill + (0.60 * diffuse) + (0.18 * specular) + rim, 0.28, 1.15);

    vec2 centered = fragTexCoord * 2.0 - 1.0;
    float crystalMask = 1.0 - smoothstep(0.2, 1.0, length(centered));
    crystalMask = clamp(crystalMask + 0.05 * sin(fragLocalPos.z * 0.45), 0.0, 1.0);

    vec3 crystalTint = mix(base.rgb, col, 0.65);
    crystalTint = mix(crystalTint, vec3(0.82, 0.96, 1.0), crystalMask * 0.10);
    vec3 litColor = crystalTint * lighting;

    vec2 vUV = uv * (1.0 - uv.yx);
    float vig = pow(vUV.x * vUV.y * 15.0, 0.15);
    litColor *= vig;

    litColor = pow(max(litColor, vec3(0.0)), vec3(1.08));
    outColor = vec4(litColor * fade, base.a * fade);
}
