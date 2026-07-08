# Bluesky

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/5423612b-c0e6-4c09-b12a-ac5493ae8f30" />

Bluesky is a procedural water and sky scene. It renders a large indexed water grid with custom Vulkan buffers and shaders, then draws a shader-driven sky behind it.

## Controls

- `Left` / `Right` - orbit the camera
- `Up` / `Page Up` - zoom in
- `Down` / `Page Down` - zoom out
- `Escape` - quit

## How It Works

The example generates a high-resolution water mesh at startup, uploads it with the reusable Vulkan resource helpers, and renders it through custom water and sky pipelines. The shaders are compiled by CMake and copied into the runtime `data/` directory.

