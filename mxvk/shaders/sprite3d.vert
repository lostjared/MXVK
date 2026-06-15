#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;

layout(set = 0, binding = 1) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform Sprite3DPushConstants {
    vec4 position_size_x;
    vec4 color;
    vec4 size_y_rotation_alpha;
} pc;

void main() {
    vec2 local = in_pos * vec2(pc.position_size_x.w, pc.size_y_rotation_alpha.x);
    float s = sin(pc.size_y_rotation_alpha.y);
    float c = cos(pc.size_y_rotation_alpha.y);
    local = vec2(local.x * c - local.y * s, local.x * s + local.y * c);

    mat3 inv_view = inverse(mat3(camera.view));
    vec3 right = normalize(inv_view * vec3(1.0, 0.0, 0.0));
    vec3 up = normalize(inv_view * vec3(0.0, 1.0, 0.0));
    vec3 world = pc.position_size_x.xyz + right * local.x + up * local.y;

    gl_Position = camera.proj * camera.view * vec4(world, 1.0);
    out_uv = in_uv;
    out_color = pc.color;
}
