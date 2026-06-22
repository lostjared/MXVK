#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragViewPos;
layout(location = 3) in vec3 fragLocalPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

float stripe(vec3 p, float time) {
    float band = sin((p.y * 9.0) + (p.x - p.z) * 3.0 + time * 1.7);
    return smoothstep(0.15, 0.95, band * 0.5 + 0.5);
}

float segmentDistance(vec3 p, vec3 a, vec3 b) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

float prismEdgeMask(vec3 p) {
    vec3 v0 = vec3(-0.8, 0.0, -0.8);
    vec3 v1 = vec3(0.8, 0.0, -0.8);
    vec3 v2 = vec3(0.8, 0.0, 0.8);
    vec3 v3 = vec3(-0.8, 0.0, 0.8);
    vec3 apex = vec3(0.0, 1.3, 0.0);

    float d = segmentDistance(p, v0, v1);
    d = min(d, segmentDistance(p, v1, v2));
    d = min(d, segmentDistance(p, v2, v3));
    d = min(d, segmentDistance(p, v3, v0));
    d = min(d, segmentDistance(p, v0, apex));
    d = min(d, segmentDistance(p, v1, apex));
    d = min(d, segmentDistance(p, v2, apex));
    d = min(d, segmentDistance(p, v3, apex));

    return 1.0 - smoothstep(0.018, 0.075, d);
}

vec3 spectralColor(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 red = vec3(1.0, 0.04, 0.02);
    vec3 orange = vec3(1.0, 0.45, 0.02);
    vec3 yellow = vec3(1.0, 0.92, 0.04);
    vec3 green = vec3(0.08, 0.88, 0.18);
    vec3 blue = vec3(0.02, 0.34, 1.0);
    vec3 violet = vec3(0.62, 0.08, 1.0);

    if (t < 0.20) {
        return mix(red, orange, t / 0.20);
    }
    if (t < 0.40) {
        return mix(orange, yellow, (t - 0.20) / 0.20);
    }
    if (t < 0.60) {
        return mix(yellow, green, (t - 0.40) / 0.20);
    }
    if (t < 0.80) {
        return mix(green, blue, (t - 0.60) / 0.20);
    }
    return mix(blue, violet, (t - 0.80) / 0.20);
}

void main() {
    float time = ubo.fx.x;
    float alphaScale = clamp(ubo.fx.w, 0.0, 1.0);

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(-fragViewPos);
    vec3 lightPos = vec3(0.0, 3.8, 0.7);
    vec3 L = normalize(lightPos - fragViewPos);
    vec3 R = reflect(-L, N);

    float ambient = 0.18;
    float diffuse = max(dot(N, L), 0.0);
    float specular = pow(max(dot(R, V), 0.0), 96.0);
    float broadSpecular = pow(max(dot(R, V), 0.0), 20.0) * 0.18;
    float rim = pow(1.0 - max(dot(N, V), 0.0), 1.55);
    float internal = stripe(fragLocalPos, time);
    float facetPulse = 0.5 + 0.5 * sin(dot(fragLocalPos, vec3(6.2, 9.7, -4.3)) + time * 1.25);
    float depthPulse = 0.5 + 0.5 * sin(fragLocalPos.y * 8.0 - fragLocalPos.x * 5.0 + fragLocalPos.z * 3.5 - time * 0.9);

    float beamCenter = fragLocalPos.y - (0.30 - fragLocalPos.x * 0.44);
    float beamMask = 1.0 - smoothstep(0.035, 0.23, abs(beamCenter));
    float prismBody = smoothstep(-0.78, -0.12, fragLocalPos.x) * (1.0 - smoothstep(0.10, 0.78, fragLocalPos.x));
    float spectrumPosition = fragLocalPos.z * 0.42 + fragLocalPos.x * 0.36 + 0.52;
    vec3 rainbow = spectralColor(spectrumPosition);
    float rainbowStrength = beamMask * prismBody * (0.55 + diffuse * 0.35);

    vec3 deepTint = vec3(0.020, 0.070, 0.180);
    vec3 midTint = vec3(0.080, 0.430, 0.760);
    vec3 brightTint = vec3(0.860, 0.970, 1.000);
    vec3 bodyTint = mix(deepTint, midTint, 0.30 + 0.45 * facetPulse);
    bodyTint = mix(bodyTint, brightTint, 0.10 + 0.18 * depthPulse + rim * 0.18);

    vec3 glow = vec3(1.0) * (0.012 + internal * 0.028);
    vec3 edgeGlow = vec3(1.0) * rim * 0.28;
    float hardEdge = prismEdgeMask(fragLocalPos);
    vec3 prismEdgeGlow = vec3(1.0) * hardEdge * (0.62 + specular * 0.42);
    vec3 highlight = vec3(1.0) * (specular * 1.45 + broadSpecular * 0.85);
    vec3 phongGlass = bodyTint * (ambient * 0.78 + diffuse * 0.36);
    vec3 litColor = phongGlass + highlight + edgeGlow + prismEdgeGlow + glow + rainbow * rainbowStrength * 0.58;

    float fresnelAlpha = 0.045 + rim * 0.18;
    float surfaceAlpha = clamp((fresnelAlpha + internal * 0.010 + rainbowStrength * 0.030 + hardEdge * 0.12) * alphaScale, 0.020, 0.28);
    outColor = vec4(litColor, surfaceAlpha);
}
