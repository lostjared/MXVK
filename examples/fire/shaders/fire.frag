#version 450

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

layout(binding = 0) uniform sampler2D sprite_tex;

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 outColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    mat2 rotate = mat2(0.80, 0.60, -0.60, 0.80);

    for (int i = 0; i < 6; ++i) {
        value += amplitude * valueNoise(p);
        p = rotate * p * 2.03 + vec2(17.7, 9.2);
        amplitude *= 0.5;
    }

    return value;
}

vec3 fireRamp(float t) {
    vec3 ember = vec3(0.025, 0.002, 0.000);
    vec3 red = vec3(0.52, 0.018, 0.000);
    vec3 orange = vec3(1.00, 0.23, 0.012);
    vec3 gold = vec3(1.00, 0.64, 0.08);
    vec3 white = vec3(1.00, 0.93, 0.60);

    vec3 color = mix(ember, red, smoothstep(0.00, 0.30, t));
    color = mix(color, orange, smoothstep(0.24, 0.58, t));
    color = mix(color, gold, smoothstep(0.48, 0.78, t));
    color = mix(color, white, smoothstep(0.76, 1.00, t));
    return color;
}

void main() {
    vec2 resolution = vec2(max(pc.screenWidth, 1.0), max(pc.screenHeight, 1.0));
    vec2 uv = vec2(out_uv.x, 1.0 - out_uv.y);
    float aspect = resolution.x / resolution.y;
    vec2 sourceUv = vec2(0.5, 0.0);
    vec2 targetUv = vec2(pc.params.y, pc.params.z);
    targetUv = clamp(targetUv, vec2(0.08, 0.22), vec2(0.92, 0.96));
    float zoom = clamp(pc.params.w, 0.25, 4.40);

    float plumeHeight = max((targetUv.y - sourceUv.y) * zoom, 0.18);
    float y = clamp((uv.y - sourceUv.y) / plumeHeight, 0.0, 1.0);
    float belowSource = smoothstep(sourceUv.y - 0.03, sourceUv.y + 0.02, uv.y);
    float bend = smoothstep(0.08, 1.0, y);
    bend = bend * bend * (3.0 - 2.0 * bend);
    float centerLine = mix(sourceUv.x, targetUv.x, bend);
    vec2 centered = vec2((uv.x - centerLine) * aspect, y);

    float t = pc.params.x;
    float baseFlicker = 0.92 + 0.08 * sin(t * 5.7) + 0.05 * sin(t * 9.1 + 1.7);
    float centerDrift = (fbm(vec2(y * 0.9 + 2.0, t * 0.20)) - 0.5) * 0.34 * smoothstep(0.12, 0.95, y);
    float x = centered.x - centerDrift;

    vec2 flow = vec2(x * 1.15, y * 1.85 - t * 0.42);
    vec2 broadWarp = vec2(
        fbm(flow + vec2(13.2, t * 0.08)),
        fbm(flow * 1.12 + vec2(31.7, -t * 0.10))) - 0.5;
    vec2 fineFlow = vec2(x * 3.2, y * 4.1 - t * 0.82) + broadWarp * vec2(1.15, 0.72);
    float rolling = fbm(fineFlow);
    float curling = fbm(fineFlow * 1.75 + vec2(7.4, -t * 0.35));

    float width = mix(0.98, 0.22, pow(y, 1.22)) * mix(0.52, 1.72, smoothstep(0.25, 4.40, zoom));
    width += (rolling - 0.5) * 0.13 * (1.0 - y);
    float body = 1.0 - smoothstep(width * 0.52, width, abs(x));
    float topFade = smoothstep(1.02, 0.16, y);
    float bottomEnergy = pow(1.0 - y, 0.20);

    float tongueField = rolling * 0.64 + curling * 0.36 + body * 0.30 - y * 0.28;
    float tongues = smoothstep(0.43, 0.88, tongueField);
    float flame = body * topFade * bottomEnergy * mix(0.45, 1.24, tongues);

    float coreWidth = mix(0.34, 0.06, smoothstep(0.04, 0.58, y));
    float hotCore = (1.0 - smoothstep(coreWidth * 0.35, coreWidth, abs(x + broadWarp.x * 0.16)));
    hotCore *= smoothstep(0.00, 0.10, y) * smoothstep(0.72, 0.18, y);
    flame += hotCore * 0.45 * baseFlicker;

    flame *= belowSource;
    float heat = clamp(flame * 1.12, 0.0, 1.0);
    vec3 color = fireRamp(heat);
    color = mix(color, vec3(0.20, 0.36, 1.00), hotCore * smoothstep(0.0, 0.16, y) * 0.18);

    float smokeNoise = fbm(vec2(x * 1.35 + broadWarp.x * 0.6, y * 1.55 - t * 0.17));
    float smoke = smoothstep(0.64, 0.96, smokeNoise + y * 0.28) * smoothstep(0.36, 0.92, y);
    smoke *= 1.0 - clamp(flame * 1.5, 0.0, 1.0);
    smoke *= belowSource;
    vec3 smokeColor = vec3(0.050, 0.047, 0.043) * smoke;

    vec3 source = texture(sprite_tex, uv).rgb;
    vec3 background = source * 0.02 + vec3(0.006, 0.002, 0.000) + vec3(0.08, 0.018, 0.000) * pow(1.0 - uv.y, 2.4);
    vec3 finalColor = background + smokeColor + color * heat * 1.18;

    float vignette = smoothstep(1.25, 0.22, length(vec2((uv.x - 0.5) * aspect, uv.y - 0.46)));
    finalColor *= mix(0.72, 1.12, vignette);

    outColor = vec4(finalColor, 1.0);
}
