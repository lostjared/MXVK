# 3D Math Texture

3D Math Texture is the textured version of the software cube test. It uses the same CPU-side 3D path as `3dmath_cube`, then adds PNG loading and software UV sampling.
The software framebuffer remains fixed at 1280x720 and its Vulkan sprite is stretched to fit the window.

## Controls

- `Escape` - quit

## Inputs

- `--filename <file.png>` or `--texture <file.png>` - texture image for the cube

## How It Works

The example transforms and rasterizes a cube in software, samples the selected PNG texture per triangle, and uploads the resulting frame as a sprite. It is useful for checking texture coordinates, clipping, and software raster output inside the MXVK render loop.
