# 3D Math Cube

3D Math Cube extends the software math test into a rotating cube. It demonstrates matrix transforms, backface culling, depth sorting, diffuse face shading, filled triangle rasterization, and line clipping before presenting through MXVK.
The software framebuffer defaults to 1280x720 and its Vulkan sprite is
stretched to fit the window. Use `--framebuffer WidthxHeight` to override it.

## Controls

- `Escape` - quit

## How It Works

The cube is generated in code and rendered through the CPU-side `mxvk/mxvk_math.h` pipeline. The final software framebuffer is uploaded to a Vulkan sprite each frame, making this a useful regression test for the math and raster helper code.
