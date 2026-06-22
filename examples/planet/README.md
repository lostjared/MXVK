# Planet Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/91c3da85-10cc-4a10-aeaf-98b7d2d84ccd" />

Planet Example renders a rotating Saturn model with a texture-mapped ring and a controllable orbital camera. It is a compact example of loading a compressed model, disabling backface culling, and driving the view from mouse input.
The scene also layers a matrix-style green rain backdrop behind the planet using a live `VK_Sprite` updated from an SDL surface every frame.

## Controls

- `Left mouse drag` - orbit the camera around the planet
- `Mouse wheel` - zoom in or out
- `Escape` - quit

## How It Works

The example loads `saturn.mxmod.z` and its texture manifest, then feeds a continuously rotating model matrix into the standard MXVK model pipeline. In parallel, it builds a glyph atlas from `keifont.ttf`, animates the rain into an off-screen surface, and uploads that surface into a sprite that is rendered before the planet. The camera is placed slightly above the equator so the ring is visible from the start.
