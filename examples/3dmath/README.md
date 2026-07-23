# 3D Math

3D Math is a minimal visual test for the software 3D helpers in `mxvk/mxvk_math.h`. It builds a small rotating triangle through `vec4D`, `Mat4D`, `RenderList`, and `PipeLine`, then uploads the result through the normal MXVK window path.
The software framebuffer remains fixed at 1280x720 and its Vulkan sprite is stretched to fit the window.

## Controls

- `Escape` - quit

## How It Works

The example exercises matrix transforms, projection, line drawing, and the CPU-side render-list path without loading external assets. It is intended as a compact smoke test for the engine math layer rather than a full application.
