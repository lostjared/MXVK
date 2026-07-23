# 3D Math PLG Loader

This example loads the PLG mesh supplied with `--filename`, then lights and
rasterizes it through the CPU-side MXVK math pipeline. When no filename is
supplied, it displays the bundled `data/sphere.plg`. Software rasterization
defaults to a 1280x720 framebuffer, which is stretched to fill the Vulkan
window.

## Controls

- `Escape` - quit
- `Left mouse drag` - rotate the object
- `Mouse wheel` - zoom in and out
- `Space` - pause or resume automatic rotation

Use `--framebuffer WidthxHeight` to select the internal software-rendering
resolution independently of the window size. The default is 1280x720:

```bash
./run.pl 3dmath_plg_loader --framebuffer 1920x1080
```

Run the bundled sphere from the repository root:

```bash
./run.pl 3dmath_plg_loader
```

Load another PLG file:

```bash
./run.pl 3dmath_plg_loader --filename /absolute/path/to/model.plg
```

Map a PNG texture using the model's PLG texture coordinates:

```bash
./run.pl 3dmath_plg_loader \
    --filename /absolute/path/to/model.plg \
    --texture /absolute/path/to/texture.png
```

When `--texture` is omitted, the loader uses its UV-based color gradient.
PNG textures automatically receive a complete mip chain. The software
rasterizer selects and blends mip levels from the projected texture footprint
to reduce shimmering and moiré on small or oblique triangles.

Texture coordinates use perspective-correct interpolation of `u/z`, `v/z`,
and `1/z` by default, preventing texture swimming across triangle diagonals.
Pass `--nowarpfix` to disable the correction and use affine screen-space
interpolation:

```bash
./run.pl 3dmath_plg_loader \
    --filename /absolute/path/to/model.plg \
    --texture /absolute/path/to/texture.png \
    --nowarpfix
```
