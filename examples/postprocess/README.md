# Postprocess

Postprocess demonstrates the `mxvk::VK_Window` multi-pass post-processing path. It renders a simple generated scene, then applies the fragment shaders listed in `data/shaders.txt`.

## Controls

- `Escape` - quit

## How It Works

The example loads `invert.frag.spv` and `scanline.frag.spv` as a post-processing chain with `attachPostProcessingShaders(...)`. Intermediate render targets are used between effects, so this is the compact sample for testing chained full-screen fragment shaders.

