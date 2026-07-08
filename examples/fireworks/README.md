# Fireworks

Fireworks is a point-sprite particle burst demo. It uses `mxvk::VK_PointSpriteBatch` to draw additive star particles that expand and fade over time.

## Controls

- `Space` - trigger a new explosion
- `Escape` - quit

## How It Works

The demo keeps particle state on the CPU, uploads point-sprite vertices each frame, and renders them through the reusable point-sprite batch pipeline. CMake compiles the local point-sprite shaders and copies the star texture into the runtime data directory.

