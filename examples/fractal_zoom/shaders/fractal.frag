#version 450
#extension GL_ARB_gpu_shader_fp64 : enable

layout(location = 0) out vec4 outColor;

layout(std430, binding = 0) readonly buffer ReferenceOrbit {
    vec4 orbit_samples[];
};

layout(push_constant) uniform PushConstants {
    double centerX;
    double centerY;
    double inverseZoom;
    double time;
    double resolutionX;
    double resolutionY;
    int maxIterations;
    int palette;
    int orbitLength;
    int reserved;
}
pc;

const double DIRECT_RENDER_MAX_ZOOM = 1.0e15;
const float PERTURBATION_BREAKDOWN_LIMIT2 = 0.0625;
const int MAX_ADAPTIVE_REFERENCES = 32;
const int REFERENCE_ORBIT_STRIDE = 4097;

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

vec3 escapedColor(int iter, float mag2, int maxIterations, float time, int palette) {
    float smoothIter = float(iter) + 1.0 - log2(log2(max(mag2, 1.000001)));
    vec3 color = paletteColor(smoothIter, time, palette);
    float edge = clamp(1.0 - float(iter) / float(max(maxIterations, 1)), 0.0, 1.0);
    return color * (0.5 + 0.5 * edge);
}

vec2 complexMul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

dvec2 complexMul(dvec2 a, dvec2 b) {
    return dvec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

bool isInteriorPoint(dvec2 c) {
    double x = c.x;
    double y = c.y;
    double y2 = y * y;

    double x_shift = x - 0.25;
    double q = x_shift * x_shift + y2;
    if (q * (q + x_shift) <= 0.25 * y2) {
        return true;
    }

    double x_plus_one = x + 1.0;
    if (x_plus_one * x_plus_one + y2 <= 0.0625) {
        return true;
    }

    return false;
}

vec3 directMandelbrotColor(dvec2 c, int maxIterations, float time, int palette) {
    if (isInteriorPoint(c)) {
        return vec3(0.0);
    }

    dvec2 z = dvec2(0.0);
    int iter = 0;
    for (; iter < maxIterations; ++iter) {
        double mag2 = dot(z, z);
        if (mag2 > 4.0) {
            return escapedColor(iter, float(mag2), maxIterations, time, palette);
        }
        z = complexMul(z, z) + c;
    }

    return vec3(0.0);
}

vec3 perturbationMandelbrotColor(vec2 delta_c, dvec2 c, int referenceBase, int orbitLength, int maxIterations, float time, int palette, bool allowDirectFallback) {
    vec2 dz = vec2(0.0);
    int iter = 0;
    int count = min(maxIterations, orbitLength - 1);

    for (int i = 0; i < count; ++i) {
        vec2 ref_Z = orbit_samples[referenceBase + i].xy;
        dz = 2.0 * complexMul(ref_Z, dz) + complexMul(dz, dz) + delta_c;

        vec2 true_z = orbit_samples[referenceBase + i + 1].xy + dz;
        ++iter;

        float true_mag2 = dot(true_z, true_z);
        if (true_mag2 > 4.0) {
            return escapedColor(iter, true_mag2, maxIterations, time, palette);
        }

        float dz_mag2 = dot(dz, dz);
        float ref_mag2 = dot(ref_Z, ref_Z);
        if (dz_mag2 > PERTURBATION_BREAKDOWN_LIMIT2 || (ref_mag2 > 0.0 && dz_mag2 > ref_mag2 * 0.25)) {
            return allowDirectFallback ? directMandelbrotColor(c, maxIterations, time, palette) : vec3(0.0);
        }
    }

    return vec3(0.0);
}

void main() {
    dvec2 resolution = dvec2(max(pc.resolutionX, 1.0), max(pc.resolutionY, 1.0));
    dvec2 uv = (dvec2(gl_FragCoord.xy) - 0.5 * resolution) / min(resolution.x, resolution.y);
    double inverse_zoom = max(pc.inverseZoom, 0.0);
    dvec2 c = uv * inverse_zoom + dvec2(pc.centerX, pc.centerY);

    bool allow_direct = inverse_zoom > 1.0 / DIRECT_RENDER_MAX_ZOOM;
    int reference_count = min(int(orbit_samples[0].x + 0.5), MAX_ADAPTIVE_REFERENCES);
    int reference_base = -1;
    int reference_orbit_length = 0;
    vec2 reference_uv = vec2(0.0);

    for (int i = 0; i < MAX_ADAPTIVE_REFERENCES; ++i) {
        if (i >= reference_count) {
            break;
        }

        int metadata_base = 1 + i * 2;
        vec4 bounds = orbit_samples[metadata_base];
        if (float(uv.x) >= bounds.x && float(uv.x) <= bounds.z && float(uv.y) >= bounds.y && float(uv.y) <= bounds.w) {
            vec4 metadata = orbit_samples[metadata_base + 1];
            reference_uv = metadata.xy;
            reference_base = int(metadata.z + 0.5);
            reference_orbit_length = int(metadata.w + 0.5);
            break;
        }
    }

    vec3 color = vec3(0.0);
    if (allow_direct || reference_base < 0 || reference_orbit_length <= 1) {
        color = directMandelbrotColor(c, pc.maxIterations, float(pc.time), pc.palette);
    } else {
        vec2 delta_c = vec2((uv - dvec2(reference_uv)) * inverse_zoom);
        color = perturbationMandelbrotColor(delta_c, c, reference_base, reference_orbit_length, pc.maxIterations, float(pc.time), pc.palette, allow_direct);
    }

    outColor = vec4(color, 1.0);
}
