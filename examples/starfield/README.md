# Starfield

Starfield is a point-sprite stress and presentation demo. It renders 50,000 layered stars with additive blending, twinkle variation, depth motion, and a warp-speed boost.

## Controls

- Hold `Space` - warp-speed boost
- `Escape` - quit

## How It Works

The example stores star position, speed, color, twinkle phase, and size data on the CPU, uploads point-sprite vertices each frame, and renders them with `mxvk::VK_PointSpriteBatch`.

