# Point Sprite

Point Sprite is a direct `mxvk::VK_PointSpriteBatch` sample. It renders animated Tux sprites as point primitives and grows the active sprite count while the program is running.

## Controls

- Hold `Space` - add more sprites
- `Enter` - reset to the default sprite count and size
- `Page Up` / `Page Down` - adjust sprite size
- `Escape` - quit

## How It Works

The example stores per-sprite position, velocity, depth, frame, size, and tint data, writes those values into point-sprite vertices, and lets `VK_PointSpriteBatch` handle the Vulkan buffers, descriptors, and rendering pipeline.

