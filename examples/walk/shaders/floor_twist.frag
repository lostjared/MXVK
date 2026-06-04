#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    float time_f = fragFx.w;
    vec2 tc = fragTexCoord;
    
    float rippleSpeed = 5.0;
    float rippleAmplitude = 0.03;
    float rippleWavelength = 10.0;
    float twistStrength = 1.0;
    
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
    
    outColor = mix(originalColor, twistedRippleColor, 0.5);
}