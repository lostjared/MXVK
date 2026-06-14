# Planet Example

Planet Example renders a rotating Saturn model with a texture-mapped ring and a controllable orbital camera. It is a compact example of loading a compressed model, disabling backface culling, and driving the view from mouse input.

## Controls

- `Left mouse drag` - orbit the camera around the planet
- `Mouse wheel` - zoom in or out
- `Escape` - quit

## How It Works

The example loads `saturn.mxmod.z` and its texture manifest, then feeds a continuously rotating model matrix into the standard MXVK model pipeline. The camera is placed slightly above the equator so the ring is visible from the start.

