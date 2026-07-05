#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 coord = fragTexCoord - vec2(0.5);
    float dist = length(coord);
    if (dist > 0.5) {
        discard;
    }
    float fade = 1.0 - smoothstep(0.0, 0.5, dist);
    outColor = vec4(fragFx.rgb, clamp(fragFx.a, 0.0, 1.0) * fade);
}
