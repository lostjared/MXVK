# Surface

Surface is a minimal SDL surface upload smoke test. It fills a CPU-side RGBA surface with random pixels every frame and draws the result as an MXVK sprite.

## Controls

- `Escape` - quit

## Inputs

- `--filename <shader>` - optional sprite fragment shader path

## How It Works

The example recreates the SDL surface when the swapchain size changes, updates the sprite texture from CPU memory each frame, and draws it to the full window.

