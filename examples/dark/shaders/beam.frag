#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D sprite_tex;

layout(push_constant) uniform SpritePushConstants {
    float screenWidth;
    float screenHeight;
    float spritePosX;
    float spritePosY;
    float spriteSizeW;
    float spriteSizeH;
    float effectsOn;
    float padding2;
    vec4 params;
} pc;

vec3 spectralColor(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 red = vec3(1.0, 0.02, 0.00);
    vec3 orange = vec3(1.0, 0.22, 0.00);
    vec3 yellow = vec3(1.0, 0.86, 0.00);
    vec3 green = vec3(0.02, 0.78, 0.08);
    vec3 blue = vec3(0.00, 0.30, 1.0);
    vec3 violet = vec3(0.48, 0.00, 0.86);

    if (t < 0.166667) {
        return red;
    }
    if (t < 0.333333) {
        return orange;
    }
    if (t < 0.500000) {
        return yellow;
    }
    if (t < 0.666667) {
        return green;
    }
    if (t < 0.833333) {
        return blue;
    }
    return violet;
}

void main() {
    vec2 aspect = vec2(max(pc.screenWidth / max(pc.screenHeight, 1.0), 1.0), 1.0);
    vec2 p = (out_uv - vec2(0.5)) * aspect;

    float angle = pc.params.x;
    float time = pc.params.y;
    float intensity = max(pc.params.z, 0.0);

    vec2 origin = vec2(0.02, -0.01);
    vec2 dir = normalize(vec2(cos(angle), sin(angle) * 0.68));
    vec2 normal = vec2(-dir.y, dir.x);
    vec2 q = p - origin;

    float along = dot(q, dir);
    float across = dot(q, normal);
    float beamLength = smoothstep(0.0, 0.18, along) * (1.0 - smoothstep(1.48, 1.92, along));
    float spread = 0.030 + max(along, 0.0) * 0.13;
    float band = across / max(spread, 0.001);
    float beamMask = smoothstep(1.0, 0.82, abs(band)) * beamLength;

    float spectrum = clamp((band * 0.5) + 0.5, 0.0, 1.0);
    vec3 color = spectralColor(spectrum);

    float separation = smoothstep(0.020, 0.055, abs(fract(spectrum * 6.0) - 0.5));
    float core = smoothstep(0.18, 0.0, abs(band)) * beamLength;
    vec3 beamColor = color * beamMask * (1.85 + separation * 0.20) + vec3(1.0) * core * 0.08;

    float alpha = clamp((beamMask * 0.94 + core * 0.08) * intensity, 0.0, 0.96);
    out_color = vec4(beamColor * intensity, alpha);
}
