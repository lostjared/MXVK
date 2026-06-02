#version 450

layout(location = 0) out vec4 color;
layout(location = 0) in vec2 tc;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;
    vec2 spritePos;
    vec2 spriteSize;
    vec4 params;
} pc;

layout(binding = 0) uniform sampler2D samp;

float pingPong(float x, float lengthVal) {
    float modVal = mod(x, lengthVal * 2.0);
    return modVal <= lengthVal ? modVal : lengthVal * 2.0 - modVal;
}

void main(void) {
    float time_f = pc.params.x;
    vec2 iResolution = pc.screenSize;

    vec2 uv = tc - 0.5;
    float distanceVal = length(uv);
    float time_t = pingPong(time_f, 7.0);

    float pulse = 1.0 + 0.5 * sin(time_t * 2.0 + distanceVal * 20.0);
    uv *= pulse;

    // Keep aspect ratio stable across non-square windows.
    float aspect = iResolution.x / max(iResolution.y, 1.0);
    uv.x *= aspect;
    uv += vec2(sin(0.5 * time_t));
    uv.x /= aspect;

    uv += 0.5;
    color = texture(samp, uv);
}
