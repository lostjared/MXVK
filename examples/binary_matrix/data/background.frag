#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D samp;

layout(push_constant) uniform PushConstants {
    float screenWidth;
    float screenHeight;
    float spritePosX;
    float spritePosY;
    float spriteSizeW;
    float spriteSizeH;
    float effectsOn;
    float padding2;
    float params[4];
} pc;

void main(void) {
    vec2 iResolution = vec2(max(pc.screenWidth, 1.0), max(pc.screenHeight, 1.0));
    vec4 iMouse = vec4(pc.params[1], pc.params[2], pc.params[3], 0.0);
    float time_f = pc.params[0];
    vec2 tc = fragTexCoord;

    vec2 px = 1.0 / max(iResolution, vec2(1.0));
    vec2 p = tc - 0.5;
    vec2 mouseUV = (iMouse.z > 0.0) ? (iMouse.xy / max(iResolution, vec2(1.0))) : vec2(0.5);
    vec2 mouseP = mouseUV * 2.0 - 1.0;
    p -= mouseP * 0.08;
    float t = time_f;
    float pulse = 0.5 + 0.5 * sin(t * 1.7 + length(p) * 20.0);
    vec2 dir = normalize(p + vec2(0.0001));
    vec3 c = texture(samp, tc).rgb * 0.55;
    vec3 bloom = vec3(0.0);
    for (int i = 1; i <= 8; ++i) {
        float f = float(i);
        vec2 o = dir * px * f * (5.0 + pulse * 14.0);
        bloom += texture(samp, clamp(tc + o, 0.0, 1.0)).rgb / f;
        bloom += texture(samp, clamp(tc - o, 0.0, 1.0)).rgb / f;
    }
    bloom *= 0.16;
    vec3 tint = 0.6 + 0.4 * cos(vec3(0.0, 2.0, 4.0) + t + pulse * 3.0);
    float halo = pow(max(0.0, 1.0 - length(p) * 1.7), 2.2);
    c += bloom * tint * 1.7 + tint * halo * 0.18;
    c += texture(samp, clamp(tc + (mouseP - p) * 0.02, 0.0, 1.0)).rgb * smoothstep(1.25, 0.0, length(p - mouseP)) * 0.18;
    c = mix(c, 1.0 - c, smoothstep(0.92, 1.0, pulse) * 0.18);
    outColor = vec4(c, 1.0);
}
