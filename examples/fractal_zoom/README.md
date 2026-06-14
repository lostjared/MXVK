# Fractal Zoom

Fractal Zoom renders a Mandelbrot-style fractal with a custom fragment shader and exposes interactive navigation over the complex plane. The shader uses push constants for the current center, zoom, palette, iteration count, and elapsed time.

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

The example draws a single full-screen triangle and lets the fragment shader compute the fractal color per pixel. On MoltenVK it uses `float` push constants; on other backends it uses `double` for higher precision. The CPU side only handles camera navigation and parameter updates.

