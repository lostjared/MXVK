# Stencil Surface

Stencil Surface is a CPU surface upload demo that draws a randomized star-shaped mask into an SDL surface, uploads it to an MXVK sprite, and redraws it after resize.

## Controls

- `Escape` - quit

## How It Works

The example computes a star polygon on the CPU, fills pixels inside it with random colors, fills the background with a dark color, and updates a Vulkan sprite texture from the SDL surface each frame.

