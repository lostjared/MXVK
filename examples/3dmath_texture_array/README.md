# 3D Math Texture Array

3D Math Texture Array renders a spinning 3×3×3 lattice of textured cubes. It uses the same CPU-side 3D path as `3dmath_cube`, then adds PNG loading, software UV sampling, and scene-wide face sorting.

## Controls

- `Escape` - quit
- `Mouse wheel` - zoom in or out

## Inputs

- `--filename <file.png>` or `--texture <file.png>` - texture image for the cubes

## How It Works

The example transforms and rasterizes 27 cubes in software, sorts all visible faces back-to-front, samples the selected PNG texture per triangle, and uploads the resulting frame as a sprite. The entire lattice continues spinning while the mouse wheel changes the camera distance.
