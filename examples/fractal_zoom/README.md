# Fractal Zoom

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/2c9f34a0-e68d-4b33-b893-c891083c0559" />

Fractal Zoom renders a Mandelbrot-style fractal with a custom fragment shader and exposes interactive navigation over the complex plane. The example is opt-in because it uses Boost.Multiprecision for high-precision camera and reference-orbit state.

Build it with:

```bash
cmake -S . -B build -DFRACTAL_ZOOM=ON
cmake --build build --target fractal_zoom -j
```

## Controls

- `Mouse wheel` - zoom in and out at the cursor position
- `Left mouse drag` - pan the fractal
- `A` / `D` or `Left` / `Right` - pan horizontally
- `W` / `S` or `Up` / `Down` - pan vertically
- `Z` - zoom in continuously
- `X` - zoom out continuously
- `1` - jump to the default view
- `2` - jump to a tighter preset view
- `3` - jump to a deep zoom preset
- `+` / `=` - increase iteration count
- `-` - decrease iteration count
- `[` / `]` - switch color palettes
- `R` - reset the view
- `Escape` - quit

## How It Works

The example draws a single full-screen triangle and lets the fragment shader compute the fractal color per pixel. The CPU keeps the camera center and zoom as `boost::multiprecision::cpp_dec_float_100`, starts with coarse viewport tiles, tests each tile with the same f32 perturbation math used by the shader, and subdivides unstable tiles breadth-first until they are stable or the reference budget/depth limit is reached.

Each final leaf tile uploads bounds, reference UV, orbit offset, and orbit length through the per-frame storage buffer. The shader finds the leaf tile covering the current pixel and perturbs from that tile's local reference orbit instead of using one global reference.

For interactive performance, reference-orbit data is cached, skipped entirely while the direct shader path is accurate, and rebuilt at most every 100 ms while the view is changing. The adaptive real-time profile uses up to 32 leaf references, depth 4, five perturbation validation samples per tile, and a capped validation iteration count.

The direct shader path is used only while absolute coordinates are still safe to represent on the GPU. At deeper zooms, perturbation avoids using low-precision absolute coordinates; if a perturbation breaks down, the shader fails closed instead of producing square-block color garbage.
