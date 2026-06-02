#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    // World-space wall UVs: keeps texture tiling continuous across segments.
    // For X-facing walls, tile along Z and Y. For Z-facing walls, tile along X and Y.
    vec3 n = normalize(fragNormal);
    vec2 wallUv = (abs(n.x) > abs(n.z)) ? vec2(fragWorldPos.z, fragWorldPos.y)
                                         : vec2(fragWorldPos.x, fragWorldPos.y);

    const float wallTileScale = 0.25;
    outColor = texture(texSampler, wallUv * wallTileScale);
}
