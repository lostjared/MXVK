# Compute Shader Camera Effects

This example captures frames from a camera, runs them through a Vulkan compute pipeline, and displays the processed output. The shader list is read from `data/index.txt`, so the same executable can swap between several compute effects without rebuilding.

## Controls

- `Up` - load the previous compute shader from `index.txt`
- `Down` - load the next compute shader from `index.txt`
- `Left` / `Right` - change the mode of `acidcam_filters.spv`
- `Escape` - quit

## How It Works

Frames arrive from OpenCV, then get uploaded into a Vulkan storage image. If CUDA interop is available, the example can keep the frame on the GPU; otherwise it falls back to a host upload path. The selected compute shader processes the image, and the result is drawn back to the swapchain with a simple full-screen presentation pass.
