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

const float TAU = 6.28318530718;

vec3 chrome(float t) {
    return vec3(0.5) + vec3(0.5) * cos(TAU * (t + vec3(0.0, 0.1, 0.2)));
}

void main() {
    float iTime = pc.params.x;
    vec2 iResolution = vec2(max(pc.screenWidth, 1.0), max(pc.screenHeight, 1.0));

    float pulse = 0.5 + 0.5 * sin(iTime * 0.85);
    float drift = 0.5 + 0.5 * sin(iTime * 0.37 + 1.6);
    float shimmer = 0.5 + 0.5 * sin(iTime * 1.7 + 0.4);
    float flash = pow(0.5 + 0.5 * sin(iTime * 0.55 - 0.8), 4.0);

    float aspect = iResolution.x / iResolution.y;
    vec2 uv = out_uv * 2.0 - vec2(1.0);
    uv.x *= aspect;

    uv /= 1.0 + pulse * 0.18;

    float r = length(uv);
    float angle = atan(uv.y, uv.x);

    float ripple_freq = 12.0 + floor(pulse * 7.0 + 0.5);
    float ripple = sin(angle * ripple_freq + iTime * 1.15 + drift * 1.4) * (0.055 + drift * 0.045);
    ripple += sin(angle * 25.0 - iTime * 2.0 + shimmer * 2.0) * (0.018 + shimmer * 0.025);

    float wave_speed = 3.2 + drift * 1.5;
    float wave_freq = 18.0 + shimmer * 5.0;
    float wave = sin(r * wave_freq - iTime * wave_speed - pulse * 2.0 + ripple * 12.0);

    vec2 warp_off = vec2(
        sin(r * 7.0 - iTime * 1.5) * drift * 0.025,
        cos(r * 7.0 + iTime * 1.2) * drift * 0.025);
    vec2 warped_uv = out_uv + warp_off + vec2(ripple * (0.02 + pulse * 0.03));

    float shift = ripple * 0.18 + wave * 0.012 + shimmer * 0.006;
    vec2 split_dir = vec2(cos(iTime * 0.25), sin(iTime * 0.25));
    vec2 uv_r = clamp(warped_uv + split_dir * shift, vec2(0.0), vec2(1.0));
    vec2 uv_g = clamp(warped_uv, vec2(0.0), vec2(1.0));
    vec2 uv_b = clamp(warped_uv - split_dir * shift, vec2(0.0), vec2(1.0));

    vec3 col;
    col.r = texture(sprite_tex, uv_r).r;
    col.g = texture(sprite_tex, uv_g).g;
    col.b = texture(sprite_tex, uv_b).b;

    vec3 chrome_col = chrome(r - iTime * 0.3 + ripple + pulse * 0.35);
    float chrome_mask = smoothstep(0.12, 1.0, wave);
    col = mix(col, col * chrome_col, chrome_mask * 0.4);

    col += wave * ripple * (2.0 + shimmer * 2.5);

    float grey = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(grey), col, 1.15 + pulse * 0.25);

    float core_glow = exp(-r * 5.0) * (1.0 + pulse * 0.4);
    col += vec3(0.95, 0.95, 1.0) * core_glow * 0.25;

    float vignette = 1.0 - length((out_uv - 0.5) * 1.6) * (0.34 + pulse * 0.18);
    col *= clamp(vignette, 0.0, 1.0);

    col *= 0.84 + drift * 0.14;
    col = mix(col, vec3(1.0) - col, smoothstep(0.96, 1.0, flash) * 0.35);

    out_color = vec4(clamp(col, 0.0, 1.0), 1.0) * clamp(pc.params.w, 0.0, 1.0);
}
