#version 450

layout(push_constant) uniform PushConstants {
    float time;
    float width;
    float height;
    float frame;
} pc;

vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2(3.0, -1.0),
    vec2(-1.0, 3.0)
);

layout(location = 0) out vec2 fragUv;

void main() {
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    fragUv = position * 0.5 + 0.5;
}
