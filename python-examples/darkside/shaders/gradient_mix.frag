#version 450

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform sampler2D screen_texture;

layout(push_constant) uniform PushConstants {
    float screen_width;
    float screen_height;
    float sprite_x;
    float sprite_y;
    float sprite_width;
    float sprite_height;
    float effects_enabled;
    float padding;
    vec4 params;
} pc;

const float TAU = 6.28318530718;

vec3 shiftingGradient(vec2 uv, float time) {
    float aspect = max(pc.screen_width, 1.0) / max(pc.screen_height, 1.0);
    vec2 centered = vec2((uv.x - 0.5) * aspect, uv.y - 0.5);
    float drift = time * 0.045;
    float field = centered.x * 0.42 + centered.y * 0.34 + drift;
    field += sin(centered.x * 3.4 - centered.y * 2.1 + time * 0.18) * 0.075;
    vec3 spectrum = 0.5 + 0.5 * cos(TAU * (field + vec3(0.00, 0.31, 0.63)));
    vec3 dark_base = vec3(0.012, 0.008, 0.035);
    return mix(dark_base, spectrum, 0.40);
}

void main() {
    vec2 uv = out_uv;
    float time = pc.params.x;
    vec3 scene = texture(screen_texture, uv).rgb;
    float energy = max(scene.r, max(scene.g, scene.b));
    float content_mask = smoothstep(0.008, 0.14, energy);

    vec3 background = shiftingGradient(uv, time);
    vec2 refraction_offset = vec2(scene.b - scene.r, scene.g - scene.b) * 0.026;
    vec3 refracted_background = shiftingGradient(clamp(uv + refraction_offset, vec2(0.0), vec2(1.0)), time + energy * 0.8);

    float luminous_mask = smoothstep(0.38, 0.82, energy);
    vec3 dark_glass = refracted_background * 0.20 + background * 0.09 + scene * 1.02;
    vec3 luminous_content = refracted_background * 0.20 + scene * 1.38;
    vec3 transparent_content = mix(dark_glass, luminous_content, luminous_mask);
    vec3 color = mix(background, transparent_content, content_mask * 0.90);

    vec2 vignette_uv = uv * (1.0 - uv.yx);
    float vignette = clamp(pow(vignette_uv.x * vignette_uv.y * 18.0, 0.22), 0.0, 1.0);
    color *= mix(0.72, 1.0, vignette);

    out_color = vec4(color, 1.0);
}
