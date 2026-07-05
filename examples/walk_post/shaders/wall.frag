#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    // World-space wall UVs: keep tiling continuous across segments and all prism faces.
    // Choose projection based on dominant world-space normal axis.
    vec3 n = normalize(fragNormal);
    vec3 an = abs(n);
    vec2 wallUv;
    if (an.y >= an.x && an.y >= an.z) {
        wallUv = vec2(fragWorldPos.x, fragWorldPos.z);
    } else if (an.x > an.z) {
        wallUv = vec2(fragWorldPos.z, fragWorldPos.y);
    } else {
        wallUv = vec2(fragWorldPos.x, fragWorldPos.y);
    }

    const float wallTileScale = 0.25;
    outColor = texture(texSampler, wallUv * wallTileScale);
}
