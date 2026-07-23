# PLG Primitives

This directory contains centered, triangulated primitive meshes for
`mxvk::mxObject::LoadPLG`. Vertex rows include `u v` texture coordinates,
outward-facing triangle winding, and the active, visible `0x0003` state.

The collection includes:

- cube
- pyramid
- tetrahedron
- octahedron
- icosahedron
- triangular prism
- cylinder
- cone
- UV sphere
- capsule
- torus

Regenerate all files after changing their topology or resolution:

```bash
python3 models/plg/generate_primitives.py
```
