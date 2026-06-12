#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float centerX;
    float centerY;
    float zoom;
    float time;
    float resolutionX;
    float resolutionY;
    int maxIterations;
    int palette;
    int aaSamples;
    int reserved;
} pc;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 paletteColor(float smoothIter, float t, int palette) {
    if (palette == 1) {
        float hue = fract(smoothIter * 0.018 + t * 0.72);
        return hsv2rgb(vec3(hue, 0.80, 1.0));
    }
    if (palette == 2) {
        float v = 0.5 + 0.5 * cos(6.2831853 * (smoothIter * 0.012 + t * 0.48));
        return vec3(v * 0.8, 0.3 + v * 0.6, 1.0 - v * 0.5);
    }
    float hue = fract(smoothIter * 0.022 + t * 1.08);
    return hsv2rgb(vec3(hue, 0.9, 1.0));
}

bool isInteriorPoint(vec2 c) {
    float x = c.x;
    float y = c.y;
    float y2 = y * y;

    float x_shift = x - 0.25;
    float q = x_shift * x_shift + y2;
    if (q * (q + x_shift) <= 0.25 * y2) {
        return true;
    }

    float x_plus_one = x + 1.0;
    if (x_plus_one * x_plus_one + y2 <= 0.0625) {
        return true;
    }

    return false;
}

vec3 mandelbrotColor(vec2 c, int maxIterations, float time, int palette) {
    if (isInteriorPoint(c)) {
        return vec3(0.0);
    }

    vec2 z = vec2(0.0);
    float zx2 = 0.0;
    float zy2 = 0.0;

    int iter = 0;
    for (int i = 0; i < maxIterations; ++i) {
        if (zx2 + zy2 > 4.0) {
            break;
        }
        float zx = z.x;
        float zy = z.y;
        float next_x = zx2 - zy2 + c.x;
        float next_y = 2.0 * zx * zy + c.y;
        z = vec2(next_x, next_y);
        zx2 = next_x * next_x;
        zy2 = next_y * next_y;
        ++iter;
    }

    if (iter >= maxIterations) {
        return vec3(0.0);
    }

    float mag2 = max(zx2 + zy2, 1.000001);
    float smoothIter = float(iter) + 1.0 - log2(log2(mag2));
    vec3 color = paletteColor(smoothIter, time, palette);

    float edge = clamp(1.0 - float(iter) / float(max(maxIterations, 1)), 0.0, 1.0);
    color *= 0.5 + 0.5 * edge;
    return color;
}

void main() {
    vec2 resolution = vec2(max(pc.resolutionX, 1.0), max(pc.resolutionY, 1.0));
    vec2 uv = (gl_FragCoord.xy - 0.5 * resolution) / min(resolution.x, resolution.y);
    vec2 c = uv / max(pc.zoom, 1.0e-18) + vec2(pc.centerX, pc.centerY);
    vec3 color = mandelbrotColor(c, pc.maxIterations, pc.time, pc.palette);
    outColor = vec4(color, 1.0);
}
