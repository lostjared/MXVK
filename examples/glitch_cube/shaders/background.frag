#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D samp;

layout(push_constant) uniform PushConstants {
    float screenWidth;
    float screenHeight;
    float spritePosX;
    float spritePosY;
    float spriteSizeW;
    float spriteSizeH;
    float padding1;
    float padding2;
    float params[4];
} pc;

void main() {
    float t = pc.params[0];

    vec2 uv = fragTexCoord;
    uv.x += 0.013 * sin(uv.y * 34.0 + t * 2.8);
    uv.y += 0.010 * cos(uv.x * 23.0 - t * 2.1);

    vec3 c0 = texture(samp, uv).rgb;
    vec3 c1 = texture(samp, uv + vec2(0.005, -0.003)).rgb;
    vec3 c2 = texture(samp, uv + vec2(-0.004, 0.004)).rgb;

    vec3 col = vec3(c0.r, c1.g, c2.b);
    float vign = smoothstep(1.12, 0.25, length((fragTexCoord - 0.5) * vec2(pc.screenWidth / pc.screenHeight, 1.0)));
    col *= (0.70 + 0.30 * vign);

    outColor = vec4(col, 1.0);
}
