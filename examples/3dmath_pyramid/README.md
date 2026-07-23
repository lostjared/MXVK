# 3D Math Pyramid

This example loads an indexed pyramid from `data/pyramid.plg` with
`mxObject::LoadPLG`, then rotates, lights, and rasterizes its triangles with
smooth vertex-color gradients through the CPU-side MXVK math pipeline. Its
geometry, triangle layout, and face-local UV coordinates match
`models/obj/pyramid.obj`.
The software framebuffer remains fixed at 1280x720 and its Vulkan sprite is
stretched to fit the window.

## Controls

- `Escape` - quit
- `Mouse wheel` - zoom in and out

Run it from the repository root:

```bash
./run.pl 3dmath_pyramid
```
