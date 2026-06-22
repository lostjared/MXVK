#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragParam;
layout(location = 1) out float fragKind;
layout(location = 2) out float fragChannel;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

vec3 rotateY(vec3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

vec3 prismPoint(vec3 p, float angle) {
    const float MODEL_SCALE = 1.84375;
    return rotateY((p + vec3(0.0, -0.65, 0.0)) * MODEL_SCALE, angle);
}

vec3 ribbonSide(vec3 dir) {
    vec3 side = vec3(0.0, 1.0, 0.0) - dir * dot(vec3(0.0, 1.0, 0.0), dir);
    if (dot(side, side) < 0.0001) {
        side = vec3(1.0, 0.0, 0.0) - dir * dot(vec3(1.0, 0.0, 0.0), dir);
    }
    return normalize(side);
}

vec3 ribbonPoint(vec3 a, vec3 b, float along, float side, float width, float growth) {
    vec3 dir = normalize(b - a);
    float w = width + along * growth;
    return mix(a, b, along) + ribbonSide(dir) * side * w;
}

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
    float spin = ubo.fx.y;
    float track = step(0.5, ubo.fx.z);
    float along = inPosition.x;
    float side = inPosition.y;
    float kind = inPosition.z;

    vec3 entry = prismPoint(vec3(-0.34, 0.72, -0.18), spin);
    vec3 exit = prismPoint(vec3(0.34, 0.66, -0.10), spin);
    vec3 incomingDir = normalize(rotateY(vec3(1.0, 0.045, -0.035), spin * track));
    vec3 incomingStart = entry - incomingDir * 48.0;
    vec3 internalDir = normalize(exit - entry);

    vec3 position = vec3(0.0);
    float channel = -1.0;
    if (kind < 0.5) {
        position = ribbonPoint(incomingStart, entry + incomingDir * 0.05, along, side, 0.060, 0.0);
    } else if (kind < 1.5) {
        position = ribbonPoint(entry, exit, along, side, 0.001, 0.0);
    } else {
        channel = clamp(kind - 2.0, 0.0, 5.0);
        float t = channel / 5.0;
        vec3 baseOut = normalize(rotateY(vec3(1.0, -0.035, 0.02), spin * track));
        vec3 outDir = normalize(baseOut + vec3(0.0, mix(0.022, -0.022, t), mix(-0.002, 0.002, t)));
        position = ribbonPoint(exit + outDir * 0.006, exit + outDir * 48.0, along, side, 0.230, 0.420);
    }

    fragParam = vec2(along, side);
    fragKind = kind;
    fragChannel = channel;
    gl_Position = ubo.proj * ubo.view * vec4(position, 1.0);
}
