#version 450

layout(push_constant) uniform PushConstants {
    float time;
    float width;
    float height;
    float frame;
} pc;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 resolution = vec2(max(pc.width, 1.0), max(pc.height, 1.0));
    vec2 pixel = gl_FragCoord.xy;

    float frame_step = floor(pc.frame);
    float base_noise = hash12(pixel + vec2(frame_step * 13.0, frame_step * 7.0));
    float flicker_noise = hash12(pixel * 0.5 + vec2(pc.time * 71.0, pc.time * 37.0));
    float noise = mix(base_noise, flicker_noise, 0.35);

    float scanline = 0.9 + 0.1 * sin((pixel.y + pc.time * 240.0) * 0.9);
    vec2 centered_uv = fragUv * 2.0 - 1.0;
    float vignette = clamp(1.0 - dot(centered_uv, centered_uv) * 0.28, 0.65, 1.0);

    float static_value = noise * scanline * vignette;
    outColor = vec4(vec3(static_value), 1.0);
}
