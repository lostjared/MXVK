#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragFx;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

float pingPong(float x, float length) {
    float modVal = mod(x, length * 2.0);
    return modVal <= length ? modVal : length * 2.0 - modVal;
}

void main() {
    float time_f = fragFx.w;
    vec2 tc = fragTexCoord;
    
    vec2 uv = tc * 2.0 - 1.0;
    float len = length(uv);
    float time_t = pingPong(time_f, 10.0);
    float bubble = smoothstep(0.8, 1.0, 1.0 - len);
    bubble = sin(bubble * time_t);
    
    vec2 distort = uv * (1.0 + 0.1 * sin(time_f + len * 20.0));
    
    distort = sin(distort * time_t);
    
    vec4 texColor = texture(texSampler, distort * 0.5 + 0.5);
    outColor = mix(texColor, vec4(1.0, 1.0, 1.0, 1.0), bubble);
}