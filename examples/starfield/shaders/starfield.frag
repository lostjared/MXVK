#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D spriteTexture;

void main() {
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = length(coord);

    if (dist > 0.5) {
        discard;
    }

    vec4 texColor = texture(spriteTexture, gl_PointCoord);
    float glow = 1.0 - smoothstep(0.0, 0.5, dist);
    float core = 1.0 - smoothstep(0.0, 0.15, dist);
    vec4 coreColor = vec4(1.0, 1.0, 1.0, 1.0) * core * fragColor.a;

    outColor = texColor * fragColor * glow + coreColor * 0.3;
    outColor.a = fragColor.a * glow;
}
