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
    
    vec2 iResolution = vec2(textureSize(texSampler, 0));
    if (iResolution.y == 0.0) {
        iResolution = vec2(1.0);
    }

    vec2 uv = (tc * iResolution - 0.5 * iResolution) / iResolution.y;
    
    float t = time_f * 0.5;
    
    float radius = length(uv);
    float angle = atan(uv.y, uv.x);
    angle += t;

    float radMod = pingPong(radius + t * 0.5, 0.5);
    float wave = sin(radius * 10.0 - t * 5.0) * 0.5 + 0.5;
    
    float r = sin(angle * 3.0 + radMod * 10.0 + wave * 6.2831);
    float g = sin(angle * 4.0 - radMod * 8.0  + wave * 4.1230);
    float b = sin(angle * 5.0 + radMod * 12.0 - wave * 3.4560);
    
    vec3 col = vec3(r, g, b) * 0.5 + 0.5;
    vec3 texColor = texture(texSampler, tc).rgb;
    col = mix(col, texColor, 0.3);
    
    float alpha = 1.0; 
    outColor = vec4(col, alpha);
}