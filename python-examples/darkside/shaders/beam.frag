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

vec2 rotate2D(vec2 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

float cross2D(vec2 a, vec2 b) {
    return a.x * b.y - a.y * b.x;
}

float raySegmentT(vec2 rayOrigin, vec2 rayDir, vec2 a, vec2 b) {
    vec2 edge = b - a;
    float denom = cross2D(rayDir, edge);
    if (abs(denom) < 0.00001) {
        return -1.0;
    }

    vec2 diff = a - rayOrigin;
    float t = cross2D(diff, edge) / denom;
    float u = cross2D(diff, rayDir) / denom;
    if (t > 0.0 && u >= 0.0 && u <= 1.0) {
        return t;
    }
    return -1.0;
}

void accumulateRayHit(vec2 rayOrigin, vec2 rayDir, vec2 a, vec2 b, inout float entryT, inout float exitT) {
    float t = raySegmentT(rayOrigin, rayDir, a, b);
    if (t > 0.0) {
        entryT = min(entryT, t);
        exitT = max(exitT, t);
    }
}

void accumulateNearestHit(vec2 rayOrigin, vec2 rayDir, vec2 a, vec2 b, inout float nearestT) {
    float t = raySegmentT(rayOrigin, rayDir, a, b);
    if (t > 0.0) {
        nearestT = min(nearestT, t);
    }
}

float segmentDistance(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float beamMask(vec2 p, vec2 origin, vec2 dir, float startT, float endT, float spread, float grow, out float band) {
    vec2 q = p - origin;
    vec2 normal = vec2(-dir.y, dir.x);
    float along = dot(q, dir);
    float width = max(spread + max(along - startT, 0.0) * grow, 0.001);
    band = dot(q, normal) / width;
    float lengthMask = smoothstep(startT, startT + 0.06, along) * (1.0 - smoothstep(endT - 0.08, endT, along));
    return smoothstep(1.05, 0.70, abs(band)) * lengthMask;
}

vec2 projectPyramidPoint(vec3 localPoint, float rotation) {
    const float MODEL_SCALE = 1.84375;
    const float FOCAL_LENGTH = 2.24604;
    vec3 p = localPoint + vec3(0.0, -0.65, 0.0);
    p *= MODEL_SCALE;

    float c = cos(rotation);
    float s = sin(rotation);
    vec3 world = vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
    vec3 view = world - vec3(0.0, 0.35, 5.35);
    float invDepth = 1.0 / max(-view.z, 0.001);
    return vec2(view.x * FOCAL_LENGTH * invDepth * 0.5,
                view.y * FOCAL_LENGTH * invDepth * 0.5);
}

void main() {
    vec2 aspect = vec2(max(pc.screenWidth / max(pc.screenHeight, 1.0), 1.0), 1.0);
    vec2 p = (out_uv - vec2(0.5)) * aspect;

    float meshRotation = pc.params.x;
    float prismRotation = meshRotation * step(0.5, pc.params.w);
    float intensity = max(pc.params.z, 0.0);

    vec2 prismCenter = projectPyramidPoint(vec3(0.0, 0.55, 0.0), meshRotation);
    vec2 rayDir = normalize(vec2(1.0, -0.19));
    vec2 rayOrigin = prismCenter - rayDir * 2.1;

    vec2 b0 = projectPyramidPoint(vec3(-0.8, 0.0, -0.8), meshRotation);
    vec2 b1 = projectPyramidPoint(vec3(0.8, 0.0, -0.8), meshRotation);
    vec2 b2 = projectPyramidPoint(vec3(0.8, 0.0, 0.8), meshRotation);
    vec2 b3 = projectPyramidPoint(vec3(-0.8, 0.0, 0.8), meshRotation);
    vec2 apex = projectPyramidPoint(vec3(0.0, 1.3, 0.0), meshRotation);

    float entryT = 10.0;
    float exitT = -1.0;
    accumulateRayHit(rayOrigin, rayDir, b0, b1, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b1, b2, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b2, b3, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b3, b0, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b0, apex, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b1, apex, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b2, apex, entryT, exitT);
    accumulateRayHit(rayOrigin, rayDir, b3, apex, entryT, exitT);
    if (exitT < entryT) {
        entryT = 1.88;
        exitT = 2.10;
    }

    vec2 entryPoint = rayOrigin + rayDir * (entryT + 0.105);
    vec2 internalDir = normalize(rotate2D(rayDir, 0.22 + sin(prismRotation) * 0.10));
    vec2 internalOrigin = entryPoint + internalDir * 0.01;
    float exitInternalT = 10.0;
    accumulateNearestHit(internalOrigin, internalDir, b0, b1, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b1, b2, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b2, b3, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b3, b0, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b0, apex, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b1, apex, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b2, apex, exitInternalT);
    accumulateNearestHit(internalOrigin, internalDir, b3, apex, exitInternalT);
    if (exitInternalT > 9.0) {
        exitInternalT = max(exitT - entryT, 0.16);
    }
    vec2 exitPoint = internalOrigin + internalDir * exitInternalT;
    vec2 outgoingBase = normalize(rotate2D(internalDir, 0.34 + sin(prismRotation) * 0.20));

    float incomingBand = 0.0;
    float incomingMask = beamMask(p, rayOrigin, rayDir, 0.0, entryT + 0.18, 0.018, 0.0, incomingBand);
    float incomingCore = smoothstep(0.36, 0.0, abs(incomingBand)) * incomingMask;
    vec3 incomingColor = vec3(0.86, 0.92, 1.0) * incomingMask * 0.58 + vec3(1.0, 0.98, 0.88) * incomingCore * 0.34;

    float internalDistance = segmentDistance(p, entryPoint, exitPoint);
    float internalAlong = dot(p - entryPoint, normalize(exitPoint - entryPoint));
    float internalLength = smoothstep(-0.02, 0.04, internalAlong) * (1.0 - smoothstep(length(exitPoint - entryPoint) - 0.03, length(exitPoint - entryPoint) + 0.04, internalAlong));
    float internalMask = smoothstep(0.040, 0.0, internalDistance) * internalLength;
    vec3 internalColor = vec3(1.0, 0.96, 0.78) * internalMask * 0.52;

    float spectrum = 0.0;
    vec3 outgoingColor = vec3(0.0);
    float outgoingAlpha = 0.0;
    for (int i = 0; i < 6; ++i) {
        float channel = (float(i) + 0.5) / 6.0;
        vec2 channelDir = normalize(rotate2D(outgoingBase, mix(-0.18, 0.18, channel)));
        float band = 0.0;
        float mask = beamMask(p, exitPoint, channelDir, 0.0, 1.75, 0.019, 0.012, band);
        float core = smoothstep(0.20, 0.0, abs(band)) * mask;
        vec3 color = spectralColor(channel);
        outgoingColor += color * mask * 1.28 + color * core * 0.58;
        outgoingAlpha += mask * 0.32 + core * 0.08;
        spectrum += mask;
    }

    float hotspot = smoothstep(0.080, 0.0, length(p - entryPoint)) * 0.22 + smoothstep(0.075, 0.0, length(p - exitPoint)) * 0.18;
    vec3 beamColor = incomingColor + internalColor + outgoingColor + vec3(1.0, 0.96, 0.84) * hotspot;
    float alpha = clamp((incomingMask * 0.30 + incomingCore * 0.10 + internalMask * 0.12 + outgoingAlpha + hotspot * 0.34) * intensity, 0.0, 0.96);
    out_color = vec4(beamColor * intensity, alpha);
}
