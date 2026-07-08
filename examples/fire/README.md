# Fire

Fire is a full-screen procedural shader demo. It attaches a fragment shader as a post-processing pass and drives the effect with elapsed time and a zoom parameter.

## Controls

- `Mouse wheel` - adjust the shader zoom
- `Escape` - quit

## How It Works

The example uses `VK_Window::attachPostProcessingShader(...)` with `fire.frag.spv`, enables shader time updates, and renders through the framework post-processing path rather than drawing scene geometry.

