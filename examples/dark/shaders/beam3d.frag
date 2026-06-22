#version 450

layout(location = 0) in vec2 fragParam;
layout(location = 1) in float fragKind;
layout(location = 2) in float fragChannel;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

vec3 spectralColor(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 red = vec3(1.0, 0.02, 0.00);
    vec3 orange = vec3(1.0, 0.22, 0.00);
    vec3 yellow = vec3(1.0, 0.86, 0.00);
    vec3 green = vec3(0.02, 0.78, 0.08);
    vec3 blue = vec3(0.00, 0.30, 1.0);
    vec3 violet = vec3(0.48, 0.00, 0.86);

    if (t < 0.166667) {
        return red;
    }
    if (t < 0.333333) {
        return orange;
    }
    if (t < 0.500000) {
        return yellow;
    }
    if (t < 0.666667) {
        return green;
    }
    if (t < 0.833333) {
        return blue;
    }
    return violet;
}

float sparkle(float x, float y, float time) {
    float waveA = sin(x * 42.0 - time * 6.0 + y * 17.0);
    float waveB = sin(x * 87.0 + time * 9.0 - y * 31.0);
    return smoothstep(0.82, 1.0, waveA * 0.5 + 0.5) * smoothstep(0.76, 1.0, waveB * 0.5 + 0.5);
}

void main() {
    float edge = smoothstep(1.0, 0.0, abs(fragParam.y));
    float lengthFade = smoothstep(0.0, 0.025, fragParam.x);
    float farFade = 1.0 - smoothstep(0.78, 1.0, fragParam.x);

    vec3 color = vec3(1.0);
    float alpha = 0.0;
    if (fragKind < 0.5) {
        color = mix(vec3(0.56, 0.62, 0.68), vec3(1.0, 0.98, 0.86), smoothstep(0.45, 0.0, abs(fragParam.y)));
        alpha = edge * lengthFade * 0.54;
    } else if (fragKind < 1.5) {
        color = vec3(1.0, 0.94, 0.72);
        alpha = 0.0;
    } else {
        float channel = clamp(fragChannel / 5.0, 0.0, 1.0);
        float lane = 1.0;
        float core = smoothstep(0.72, 0.0, abs(fragParam.y));
        float hotStart = (1.0 - smoothstep(0.0, 0.22, fragParam.x)) * core;
        float glint = sparkle(fragParam.x, channel, ubo.fx.x) * (0.25 + 0.75 * core);
        vec3 rainbow = spectralColor(channel) * (2.05 + lane * 0.18 + glint * 0.55);
        color = mix(rainbow, vec3(1.0, 0.98, 0.78) * 2.15, hotStart * 0.58);
        alpha = edge * lengthFade * farFade * (0.86 + hotStart * 0.14 + glint * 0.08);
    }

    outColor = vec4(color, alpha * clamp(ubo.fx.w, 0.0, 1.0));
}
