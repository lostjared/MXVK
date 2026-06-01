#version 450
#extension GL_ARB_gpu_shader_fp64 : enable

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    double centerX;
    double centerY;
    double zoom;
    double time;
    double resolutionX;
    double resolutionY;
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

bool isInteriorPoint(dvec2 c) {
    const double x = c.x;
    const double y = c.y;
    const double y2 = y * y;

    // Main cardioid rejection.
    const double x_shift = x - 0.25;
    const double q = x_shift * x_shift + y2;
    if (q * (q + x_shift) <= 0.25 * y2) {
        return true;
    }

    // Period-2 bulb rejection around (-1, 0), radius 1/4.
    const double x_plus_one = x + 1.0;
    if (x_plus_one * x_plus_one + y2 <= 0.0625) {
        return true;
    }

    return false;
}

vec3 mandelbrotColor(dvec2 c, int maxIterations, float time, int palette) {
    if (isInteriorPoint(c)) {
        return vec3(0.0);
    }

    dvec2 z = dvec2(0.0);
    double zx2 = 0.0;
    double zy2 = 0.0;

    int iter = 0;
    for (int i = 0; i < maxIterations; ++i) {
        if (zx2 + zy2 > 4.0) {
            break;
        }
        const double zx = z.x;
        const double zy = z.y;
        const double next_x = zx2 - zy2 + c.x;
        const double next_y = 2.0 * zx * zy + c.y;
        z = dvec2(next_x, next_y);
        zx2 = next_x * next_x;
        zy2 = next_y * next_y;
        ++iter;
    }

    if (iter >= maxIterations) {
        return vec3(0.0);
    }

    float mag2 = float(max(zx2 + zy2, 1.000001));
    float smoothIter = float(iter) + 1.0 - log2(log2(mag2));
    vec3 color = paletteColor(smoothIter, time, palette);

    float edge = clamp(1.0 - float(iter) / float(max(maxIterations, 1)), 0.0, 1.0);
    color *= 0.5 + 0.5 * edge;
    return color;
}

void main() {
    dvec2 resolution = dvec2(max(pc.resolutionX, 1.0), max(pc.resolutionY, 1.0));
    dvec2 uv = (dvec2(gl_FragCoord.xy) - 0.5 * resolution) / min(resolution.x, resolution.y);
    dvec2 c = uv / max(pc.zoom, 1.0e-18) + dvec2(pc.centerX, pc.centerY);
    vec3 color = mandelbrotColor(c, pc.maxIterations, float(pc.time), pc.palette);
    outColor = vec4(color, 1.0);
}
