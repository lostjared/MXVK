#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    float time_f = fragFx.w;
    vec2 uv = vec2(atan(fragWorldPos.z, fragWorldPos.x) / 6.28318530718 + 0.5, fragWorldPos.y * 0.25);
    vec2 center = vec2(0.5, 0.5);
    vec2 offset = uv - center;
    float offsetLen = length(offset);
    vec2 offsetDir = (offsetLen > 1e-5) ? normalize(offset) : vec2(0.0, 0.0);
    float fractalFactor = sin(offsetLen * 10.0 + time_f * 2.0) * 0.1;
    vec2 fractalUV = uv + fractalFactor * offsetDir;
    float grid = abs(sin(fractalUV.x * 50.0) * sin(fractalUV.y * 50.0));
    grid = step(0.7, grid);
    float angle = atan(offset.y, offset.x) + fractalFactor * sin(time_f);
    float radius = offsetLen;
    vec2 swirlUV = center + radius * vec2(cos(angle), sin(angle));
    vec2 combinedUV = mix(swirlUV, fractalUV, 0.5);
    vec4 texColor = texture(texSampler, combinedUV);
    vec3 rainbow = vec3(0.5 + 0.5 * sin(time_f + texColor.r),
                        0.5 + 0.5 * sin(time_f + texColor.g + 2.0),
                        0.5 + 0.5 * sin(time_f + texColor.b + 4.0));
    vec3 finalColor = mix(texColor.rgb * rainbow, vec3(grid), 0.3);
    outColor = vec4(finalColor, texColor.a);
}
