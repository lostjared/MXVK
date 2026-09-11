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

void main() {
    float edge = 1.0 - smoothstep(0.82, 1.0, abs(fragParam.y));
    float lengthFade = smoothstep(0.0, 0.012, fragParam.x);

    vec3 color = vec3(1.0);
    float alpha = 0.0;
    if (fragKind < 0.5) {
        color = vec3(0.92, 0.96, 1.0);
        alpha = edge * lengthFade * 0.88;
    } else if (fragKind < 1.5) {
        color = vec3(1.0, 0.98, 0.88);
        alpha = edge * lengthFade * 0.72;
    } else {
        float channel = clamp(fragChannel / 5.0, 0.0, 1.0);
        color = spectralColor(channel) * 1.35;
        alpha = edge * lengthFade * 0.96;
    }

    outColor = vec4(color, alpha * clamp(ubo.fx.w, 0.0, 1.0));
}
