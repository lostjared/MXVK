#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in float fragDistance;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 params;
} pc;

void main() {
    float time = pc.params.x;
    float flicker = 0.82 + 0.18 * sin(time * 26.0 + fragDistance * 19.0);
    float fade = smoothstep(1.0, 0.0, fragDistance);
    vec3 hot = vec3(1.0, 0.86, 0.45);
    vec3 ember = vec3(1.0, 0.35, 0.04);
    vec3 color = mix(hot, ember, fragDistance);
    color *= flicker;

    outColor = vec4(color, fragColor.a * fade * flicker);
}
