# Static Triangle

Static Triangle is a low-level Vulkan rendering sample that draws a single full-screen triangle with a custom shader pipeline. Unlike the `hello_world` example, it pushes window size and frame count into the shaders so they can generate a more dynamic effect.

## Controls

- `Escape` - quit

## How It Works

The sample loads precompiled triangle shaders, builds a graphics pipeline on demand, and recreates it when the swapchain changes. Each frame it updates push constants with time, window dimensions, and a monotonically increasing frame index, which lets the shader produce animated output without any scene objects.

