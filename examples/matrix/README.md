# Matrix Rain

Matrix Rain recreates the classic falling-glyph effect using SDL_ttf and MXVK sprites. The glyphs are rendered into an off-screen SDL surface, tinted by depth level, and uploaded into a texture every frame.

## Controls

- `Space` - randomize the streams and clear the canvas
- `Escape` - quit

## How It Works

The sample builds a glyph set from `keifont.ttf`, lays out vertical streams to match the current window size, and updates their positions on a timer. The canvas is then uploaded to a sprite and drawn to the swapchain, so the whole effect stays inside the Vulkan presentation loop.

