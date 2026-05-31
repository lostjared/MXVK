#version 450

layout(push_constant) uniform PushConstants {
    float time;
    float aspect;
} pc;

vec3 positions[3] = vec3[](
    vec3(0.0, -0.65, 0.0),
    vec3(0.65, 0.55, 0.0),
    vec3(-0.65, 0.55, 0.0)
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.2, 1.0)
);

layout(location = 0) out vec3 fragColor;

void main() {
    float yaw = pc.time * 1.15;
    float pitch = pc.time * 0.75;

    mat3 rotate_y = mat3(
        cos(yaw), 0.0, sin(yaw),
        0.0, 1.0, 0.0,
        -sin(yaw), 0.0, cos(yaw)
    );
    mat3 rotate_x = mat3(
        1.0, 0.0, 0.0,
        0.0, cos(pitch), -sin(pitch),
        0.0, sin(pitch), cos(pitch)
    );

    vec3 world = rotate_y * rotate_x * positions[gl_VertexIndex];
    world.z -= 2.2;

    float perspective = 1.4 / max(-world.z, 0.001);
    vec2 projected = world.xy * perspective;
    float safe_aspect = max(pc.aspect, 0.001);

    gl_Position = vec4(projected.x / safe_aspect, projected.y, 0.5, 1.0);
    fragColor = colors[gl_VertexIndex];
}
