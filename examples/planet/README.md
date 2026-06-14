# Planet Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/91c3da85-10cc-4a10-aeaf-98b7d2d84ccd" />

Planet Example renders a rotating Saturn model with a texture-mapped ring and a controllable orbital camera. It is a compact example of loading a compressed model, disabling backface culling, and driving the view from mouse input.

## Controls

- `Left mouse drag` - orbit the camera around the planet
- `Mouse wheel` - zoom in or out
- `Escape` - quit

## How It Works

The example loads `saturn.mxmod.z` and its texture manifest, then feeds a continuously rotating model matrix into the standard MXVK model pipeline. The camera is placed slightly above the equator so the ring is visible from the start.

