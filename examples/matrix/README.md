# Matrix Rain

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/10f87a3f-a440-49e1-ac3d-ef686aa97dfb" />

Matrix Rain recreates the classic falling-glyph effect using SDL_ttf and MXVK sprites. The glyphs are rendered into an off-screen SDL surface, tinted by depth level, and uploaded into a texture every frame. The bundled default font is the bold `Noto Sans CJK JP` face; use `--font-path <file>` to point at another font.

## Controls

- `Space` - randomize the streams and clear the canvas
- `Escape` - quit
- `--binary` - limit the glyph set to `0` and `1`
- `--font-size <px>` - set the glyph raster size in pixels
- `--font-path <file>` - override the bundled bold font file used for glyphs

## How It Works

The sample builds a glyph set from the bundled bold `Noto Sans CJK JP` font, lays out vertical streams to match the current window size, and updates their positions on a timer. The canvas is then uploaded to a sprite and drawn to the swapchain, so the whole effect stays inside the Vulkan presentation loop.
