#version 450

layout(location = 0) in vec3 vertexColor;
layout(location = 1) in vec2 TexCoords;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texture1;
layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

vec4 distort4(vec2 tc, sampler2D samp, vec2 iResolution, float time_f) {
    vec4 ctx;
    vec2 uv = tc * 2.0 - 1.0;
    uv *= iResolution.y / iResolution.x;

    vec2 center = vec2(0.0, 0.0);
    vec2 offset = uv - center;
    float dist = length(offset);
    float pulse = sin(time_f * 2.0 + dist * 10.0) * 0.15;
    float expansion = cos(time_f * 3.0 - dist * 15.0) * 0.2;
    float spiral = sin(atan(offset.y, offset.x) * 3.0 + time_f * 2.0) * 0.1;

    vec2 morphUV = uv + normalize(offset) * (pulse + expansion);
    morphUV += vec2(cos(atan(offset.y, offset.x)), sin(atan(offset.y, offset.x))) * spiral;

    vec4 texColor = texture(samp, morphUV * 0.5 + 0.5);
    texColor.rgb *= 1.0 + 0.3 * sin(dist * 20.0 - time_f * 5.0);

    ctx = vec4(texColor.rgb, texColor.a);
    return ctx;
}

void main() {
    // Reconstruct timing and aspect from animated model/projection matrices so the
    // GLSL 450 Vulkan shader reproduces the original embedded shader behavior.
    float time_f = atan(ubo.model[2][0], ubo.model[0][0]);
    if (time_f < 0.0) {
        time_f += 6.28318530718;
    }
    float aspect = abs(ubo.proj[1][1] / max(0.0001, ubo.proj[0][0]));
    vec2 iResolution = vec2(max(0.0001, aspect), 1.0);

    outColor = vec4(vertexColor, 1.0) * texture(texture1, TexCoords);
    vec4 ctx = distort4(TexCoords, texture1, iResolution, time_f);
    outColor = ctx * outColor;
    outColor.a = 1.0;
}
