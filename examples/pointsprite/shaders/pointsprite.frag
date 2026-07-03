#version 450

layout(location = 0) in vec4 spriteData;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D spriteTexture;

void main() {
    const float gridSize = 4.0;
    const float frameCount = 16.0;

    float frame = floor(clamp(spriteData.r, 0.0, 1.0) * (frameCount - 1.0) + 0.5);
    float frameX = mod(frame, gridSize);
    float frameY = floor(frame / gridSize);
    vec2 cell = vec2(frameX, frameY) / gridSize;
    vec2 localCoord = gl_PointCoord;

    if (spriteData.g > 0.5) {
        localCoord.x = 1.0 - localCoord.x;
    }

    vec2 texCoord = cell + localCoord / gridSize;
    vec4 texColor = texture(spriteTexture, texCoord);
    float magentaDistance = distance(texColor.rgb, vec3(1.0, 0.0, 1.0));
    if (texColor.a < 0.02 || magentaDistance < 0.18) {
        discard;
    }

    float keyedAlpha = smoothstep(0.18, 0.34, magentaDistance);
    float tint = clamp(spriteData.b, 0.0, 1.0);
    outColor = vec4(texColor.rgb * tint, texColor.a * spriteData.a * keyedAlpha);
}
