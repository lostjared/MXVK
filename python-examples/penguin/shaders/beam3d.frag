#version 450

layout(location = 0) in vec2 fragParam;
layout(location = 1) in float fragKind;
layout(location = 2) in float fragChannel;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 fx;
} ubo;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);

    f = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(
        mix(a, b, f.x),
        mix(c, d, f.x),
        f.y
    );
}

float gaussian(float x, float center, float width) {
    float d = (x - center) / width;
    return exp(-0.5 * d * d);
}

vec3 wavelengthColor(float wavelength) {
    float r =
        gaussian(wavelength, 610.0, 48.0) +
        gaussian(wavelength, 700.0, 65.0) * 0.45;

    float g =
        gaussian(wavelength, 545.0, 42.0) +
        gaussian(wavelength, 585.0, 55.0) * 0.22;

    float b =
        gaussian(wavelength, 455.0, 38.0) +
        gaussian(wavelength, 410.0, 38.0) * 0.50;

    vec3 color = vec3(r, g, b);

    float maximum = max(
        max(color.r, color.g),
        color.b
    );

    if (maximum > 0.0001) {
        color /= maximum;
    }

    float violetFade = smoothstep(
        380.0,
        410.0,
        wavelength
    );

    float redFade =
        1.0 -
        smoothstep(
            690.0,
            730.0,
            wavelength
        );

    color *= violetFade * redFade;

    return color;
}

vec3 smoothSpectrum(float wavelength) {
    vec3 color = vec3(0.0);
    float weight = 0.0;

    for (int i = -6; i <= 6; ++i) {
        float offset = float(i) * 3.5;

        float sampleWeight = exp(
            -float(i * i) / 16.0
        );

        color += wavelengthColor(
            wavelength + offset
        ) * sampleWeight;

        weight += sampleWeight;
    }

    return color / max(weight, 0.0001);
}

vec3 rainbowGradient(float channel, float y, float x, float time) {
    float local = clamp(
        y * 0.5 + 0.5,
        0.0,
        1.0
    );

    float spectrumPosition = (
        channel + local
    ) / 6.0;

    spectrumPosition = clamp(
        spectrumPosition,
        0.0,
        1.0
    );

    float wavelength = mix(
        700.0,
        390.0,
        spectrumPosition
    );

    float shimmer = noise2D(
        vec2(
            x * 8.0 - time * 0.10,
            y * 12.0 + channel * 2.71
        )
    );

    wavelength += (shimmer - 0.5) * 1.5;

    vec3 color = smoothSpectrum(
        wavelength
    );

    float centerGlow = exp(
        -y * y * 1.8
    );

    color += vec3(
        1.0,
        0.96,
        0.88
    ) * centerGlow * 0.045;

    float microVariation =
        sin(
            x * 21.0 +
            y * 19.0 +
            time * 0.25
        ) * 0.012;

    color *= 1.0 + microVariation;

    return color;
}

float beamShape(float y) {
    float ay = abs(y);

    float core =
        1.0 -
        smoothstep(
            0.62,
            0.96,
            ay
        );

    float glow = exp(
        -ay * ay * 2.0
    );

    return max(
        core,
        glow * 0.60
    );
}

vec3 whiteLight(float y, float x, float time) {
    float n = noise2D(
        vec2(
            x * 5.0 - time * 0.08,
            y * 9.0
        )
    );

    vec3 cool = vec3(
        0.86,
        0.93,
        1.0
    );

    vec3 warm = vec3(
        1.0,
        0.98,
        0.90
    );

    vec3 color = mix(
        cool,
        warm,
        y * 0.25 + 0.5
    );

    color *= 0.96 + n * 0.07;

    float center = exp(
        -y * y * 8.0
    );

    color += vec3(1.0) * center * 0.18;

    return color;
}

void main() {
    float time = ubo.fx.x;

    float x = max(
        fragParam.x,
        0.0
    );

    float y = clamp(
        fragParam.y,
        -1.0,
        1.0
    );

    float beam = beamShape(y);

    float edge =
        1.0 -
        smoothstep(
            0.88,
            1.0,
            abs(y)
        );

    float lengthFade = smoothstep(
        0.0,
        0.025,
        x
    );

    vec3 color = vec3(1.0);
    float alpha = 0.0;

    if (fragKind < 0.5) {
        color = whiteLight(
            y,
            x,
            time
        );

        alpha =
            beam *
            edge *
            lengthFade *
            0.88;

    } else if (fragKind < 1.5) {
        color = mix(
            vec3(1.0, 0.96, 0.84),
            vec3(0.88, 0.94, 1.0),
            y * 0.5 + 0.5
        );

        float glow = exp(
            -y * y * 5.0
        );

        color += vec3(
            1.0,
            0.98,
            0.91
        ) * glow * 0.12;

        alpha =
            beam *
            edge *
            lengthFade *
            0.76;

    } else {
        color = rainbowGradient(
            fragChannel,
            y,
            x,
            time
        );

        float softGlow = exp(
            -y * y * 1.4
        );

        alpha =
            (
                beam * 0.85 +
                softGlow * 0.18
            ) *
            edge *
            lengthFade *
            0.96;
    }

    float intensityNoise = noise2D(
        vec2(
            x * 3.0 - time * 0.04,
            y * 6.0
        )
    );

    color *= mix(
        0.97,
        1.04,
        intensityNoise
    );

    color = pow(
        max(color, vec3(0.0)),
        vec3(0.88)
    );

    color *= 1.12;

    float globalAlpha = clamp(
        ubo.fx.w,
        0.0,
        1.0
    );

    outColor = vec4(
        color,
        clamp(alpha, 0.0, 1.0) * globalAlpha
    );
}
