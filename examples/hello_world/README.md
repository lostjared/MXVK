# Hello World

Hello World is the smallest possible MXVK rendering example. It opens a window, compiles a minimal graphics pipeline, and draws a single animated triangle.

## Controls

- `Escape` - quit

## How It Works

The example creates a graphics pipeline from `triangle.vert.spv` and `triangle.frag.spv`, then passes elapsed time and aspect ratio through push constants. The triangle is rendered as a full-screen style starter sample for the engine's low-level Vulkan path.

