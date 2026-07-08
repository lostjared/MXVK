#version 450

layout(push_constant) uniform PushConstants {
    float time;
    float aspect;
    float phase;
    float scale;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

float ring(float radius, float center, float width) {
    return 1.0 - smoothstep(width, width + 0.01, abs(radius - center));
}

void main() {
    vec2 p = uv * 2.0 - 1.0;
    p.x *= pc.aspect;

    float radius = length(p);
    float angle = atan(p.y, p.x);
    float folded = abs(cos(angle * 8.0 + sin(radius * 16.0 - pc.phase) * 0.9));
    float petal = smoothstep(0.42, 0.95, folded) * smoothstep(0.92, 0.18, radius);
    float lace = ring(radius, 0.28 + 0.035 * sin(angle * 16.0 + pc.phase), 0.035);
    float outer = ring(radius, 0.66 + 0.05 * cos(angle * 12.0 - pc.phase * 1.4), 0.045);
    float star = smoothstep(0.72, 0.93, abs(cos(angle * 5.0))) * smoothstep(0.72, 0.32, radius);
    float cutouts = smoothstep(0.08, 0.02, abs(sin(angle * 24.0 + radius * 20.0))) * smoothstep(0.35, 0.82, radius);

    float mask = max(max(petal, lace), max(outer, star));
    mask *= 1.0 - cutouts * 0.7;

    if (radius > 0.82 || mask < 0.42) {
        discard;
    }

    outColor = vec4(0.0);
}
