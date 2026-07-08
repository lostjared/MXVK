#version 450

layout(push_constant) uniform PushConstants {
    float time;
    float aspect;
    float phase;
    float scale;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= pc.aspect;

    float radius = length(p);
    float angle = atan(p.y, p.x);
    vec3 deep = vec3(0.04, 0.10, 0.18);
    vec3 gold = vec3(1.0, 0.72, 0.22);
    vec3 cyan = vec3(0.14, 0.82, 0.95);
    vec3 rose = vec3(0.92, 0.18, 0.46);
    float bands = 0.5 + 0.5 * sin(radius * 28.0 - pc.time * 1.6);
    float spokes = 0.5 + 0.5 * cos(angle * 16.0 + pc.time);
    vec3 color = mix(cyan, rose, spokes);
    color = mix(color, gold, bands * 0.55);
    color = mix(deep, color, smoothstep(0.82, 0.05, radius));
    outColor = vec4(color, 1.0);
}
