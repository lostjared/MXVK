#version 450

layout(location = 0) in vec2 tc;
layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler2D samp;
layout(set = 0, binding = 6) uniform sampler2D mat_samp;

// Matches the MXVK sprite vertex shader's push constants.
layout(push_constant) uniform SpritePushConstants {
    float screenWidth;
    float screenHeight;
    float spritePosX;
    float spritePosY;
    float spriteSizeW;
    float spriteSizeH;
    float effectsOn;
    float padding2;
    vec4 params; // x: restore_black_value, y: alpha
} pc;

void main() {
    vec4 source_color = texture(samp, tc);
    if (pc.params.x == 1.0 && all(equal(source_color, vec4(0.0, 0.0, 0.0, 1.0)))) {
        discard;
    }

    color = source_color;
    vec4 color2 = texture(mat_samp, tc);
    for (int i = 0; i < 3; ++i) {
        if (color[i] > 0.6) {
            color[i] *= color2[i] * pc.params.y;
        }
    }
}
