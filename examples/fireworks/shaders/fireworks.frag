#version 450

layout(location = 0) in vec4 particleColor;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D spriteTexture;

void main() {
    float dist = length(gl_PointCoord - vec2(0.5));
    if (dist > 0.3) {
        discard;
    }

    vec4 texColor = texture(spriteTexture, gl_PointCoord);
    outColor = texColor * particleColor;
}
