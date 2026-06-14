# Sprite Example

This is a minimal sprite rendering sample. It loads an image, stretches it to the current window size, and prints a short text overlay.

## Controls

- `Escape` - quit

## How It Works

The example creates a sprite from `intro.png`, keeps it sized to the swapchain extent, and draws it every frame before rendering the text overlay. It is the smallest possible reference for showing an image with MXVK.

