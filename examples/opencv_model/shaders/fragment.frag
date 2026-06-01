#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D texSampler;
layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

void main() {
    vec2 tc = fragTexCoord;
    float time_f = ubo.fx.x;
    const float rippleSpeed = 5.0;
    const float rippleAmplitude = 0.03;
    const float rippleWavelength = 10.0;
    const float twistStrength = 1.0;
    float radius = length(tc - vec2(0.5, 0.5));
    float ripple = sin(tc.x * rippleWavelength + time_f * rippleSpeed) * rippleAmplitude;
    ripple += sin(tc.y * rippleWavelength + time_f * rippleSpeed) * rippleAmplitude;
    vec2 rippleTC = tc + vec2(ripple, ripple);
    float angle = twistStrength * (radius - 1.0) + time_f;
    float cosA = cos(angle);
    float sinA = sin(angle);
    mat2 rotationMatrix = mat2(cosA, -sinA, sinA, cosA);
    vec2 twistedTC = (rotationMatrix * (tc - vec2(0.5, 0.5))) + vec2(0.5, 0.5);
    vec4 originalColor = texture(texSampler, tc);
    vec4 twistedRippleColor = texture(texSampler, mix(rippleTC, twistedTC, 0.5));
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.0, 0.2, 1.0));
    float diffuse = max(dot(N, lightDir), 0.0);
    float lighting = 0.72 + diffuse * 0.28;
    vec4 color = twistedRippleColor;
    color.rgb = color.rgb * lighting;
    outColor = vec4(clamp(color.rgb, vec3(0.0), vec3(1.0)), 1.0);
}
