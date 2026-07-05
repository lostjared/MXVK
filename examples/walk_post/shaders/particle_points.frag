#version 450

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5) {
        discard;
    }
    float fade = 1.0 - smoothstep(0.0, 0.5, dist);
    outColor = vec4(inColor.rgb, inColor.a * fade);
}
