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

Benchmark the CPU geometry pipeline over 60 frames:

```bash
./run.pl 3dmath_plg_loader --benchmark
```

The reported time covers matrix setup, vertex transformation, face lighting
and culling, and triangle rasterization. It excludes framebuffer clearing,
texture upload, sprite drawing, and presentation. Benchmark mode exits after
the result is printed. To keep shared scalar rasterization from hiding math
backend differences, benchmark mode uses a 320x180 software framebuffer unless
`--framebuffer` is explicitly supplied. Use `--framebuffer 1280x720` for an
end-to-end comparison at the normal rendering resolution.

For a heavier comparison, the generated `models/plg/heavy_sphere.plg` contains
32,514 vertices and 65,024 triangles:

```bash
./run.pl 3dmath_plg_loader \
    --benchmark \
    --filename "$(pwd)/models/plg/heavy_sphere.plg"
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

Texture coordinates clamp to the image edges by default, which is suitable
for cube faces and texture atlases. Add `--repeat` for horizontally wrapped
models such as spheres:

```bash
./run.pl 3dmath_plg_loader \
    --texture /absolute/path/to/texture.png \
    --repeat
```

When `--texture` is omitted, the loader uses its UV-based color gradient.
PNG textures automatically receive a complete mip chain. The software
rasterizer selects and blends mip levels from the projected texture footprint
to reduce shimmering and moiré on small or oblique triangles. Pass
`--disable-mipmap` to skip mip generation and always sample the full-resolution
texture. Use `--mip-bias <value>` to adjust automatic selection: negative
values select sharper levels and positive values select softer levels. For
example:

```bash
./run.pl 3dmath_plg_loader --mip-bias -0.75
```

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
