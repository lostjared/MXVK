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

    float prismRotation = pc.params.x;
    float intensity = max(pc.params.z, 0.0);

    vec2 origin = vec2(0.01, -0.005);
    vec2 incomingDir = normalize(vec2(1.0, -0.19));
    vec2 outgoingDir = normalize(vec2(1.0, 0.19));
    vec2 incomingNormal = vec2(-incomingDir.y, incomingDir.x);
    vec2 outgoingNormal = vec2(-outgoingDir.y, outgoingDir.x);
    float faceOffset = 0.052 + cos(prismRotation) * 0.010;
    vec2 incomingRotationOffset = incomingNormal * sin(prismRotation) * 0.014;
    vec2 outgoingRotationOffset = outgoingNormal * sin(prismRotation) * 0.014;
    vec2 incomingOrigin = origin - incomingDir * faceOffset + incomingRotationOffset;
    vec2 outgoingOrigin = origin + outgoingDir * faceOffset - outgoingRotationOffset;
    vec2 outgoingQ = p - outgoingOrigin;
    vec2 incomingQ = p - incomingOrigin;

    float outgoingAlong = dot(outgoingQ, outgoingDir);
    float outgoingAcross = dot(outgoingQ, outgoingNormal);
    float outgoingLength = smoothstep(0.0, 0.12, outgoingAlong) * (1.0 - smoothstep(1.48, 1.92, outgoingAlong));
    float outgoingSpread = 0.030 + max(outgoingAlong, 0.0) * 0.13;
    float outgoingBand = outgoingAcross / max(outgoingSpread, 0.001);
    float outgoingMask = smoothstep(1.0, 0.82, abs(outgoingBand)) * outgoingLength;

    float spectrum = clamp((outgoingBand * 0.5) + 0.5, 0.0, 1.0);
    vec3 color = spectralColor(spectrum);

    float separation = smoothstep(0.020, 0.055, abs(fract(spectrum * 6.0) - 0.5));
    float outgoingCore = smoothstep(0.18, 0.0, abs(outgoingBand)) * outgoingLength;
    vec3 outgoingColor = color * outgoingMask * (1.85 + separation * 0.20) + vec3(1.0) * outgoingCore * 0.08;

    float incomingAlong = dot(incomingQ, incomingDir);
    float incomingAcross = dot(incomingQ, incomingNormal);
    float incomingLength = smoothstep(-1.92, -1.48, incomingAlong) * (1.0 - smoothstep(-0.12, 0.0, incomingAlong));
    float incomingSpread = 0.018 - min(incomingAlong, 0.0) * 0.003;
    float incomingBand = incomingAcross / max(incomingSpread, 0.001);
    float incomingMask = smoothstep(1.12, 0.70, abs(incomingBand)) * incomingLength;
    float incomingCore = smoothstep(0.34, 0.0, abs(incomingBand)) * incomingLength;
    float prismHotspot = smoothstep(0.095, 0.0, length(incomingQ)) * 0.34;

    vec3 incomingColor = vec3(0.86, 0.92, 1.0) * incomingMask * 0.64 + vec3(1.0, 0.98, 0.88) * (incomingCore * 0.42 + prismHotspot);
    vec3 beamColor = incomingColor + outgoingColor;

    float alpha = clamp((incomingMask * 0.34 + incomingCore * 0.12 + prismHotspot * 0.32 + outgoingMask * 0.94 + outgoingCore * 0.08) * intensity, 0.0, 0.96);
    out_color = vec4(beamColor * intensity, alpha);
}
