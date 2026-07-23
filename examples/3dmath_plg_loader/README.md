# 3D Math PLG Loader

This example loads the PLG mesh supplied with `--filename`, then rotates,
lights, and rasterizes it through the CPU-side MXVK math pipeline. When no
filename is supplied, it displays the bundled `data/sphere.plg`. Software
rasterization uses a fixed 1280x720 framebuffer, which is stretched to fill
the Vulkan window.

## Controls

- `Escape` - quit
- `Mouse wheel` - zoom in and out

Run the bundled sphere from the repository root:

```bash
./run.pl 3dmath_plg_loader
```

Load another PLG file:

```bash
./run.pl 3dmath_plg_loader --filename /absolute/path/to/model.plg
```
