# 3D Math MasterPiece

3D Math MasterPiece is a variant of the MasterPiece example that keeps the original game flow but renders the falling blocks and board cells as spinning 3D cubes on a black background.

The cube blocks are rasterized through `mxvk_math` into a CPU-side frame surface, then uploaded to an MXVK sprite each frame. Menus, scoring, keyboard input, gamepad input, and high-score handling follow the original MasterPiece example.
The software cube layer defaults to 1280x720 and is stretched to fit the
window, while the menus and HUD retain their responsive window-space layout.
Use `--framebuffer WidthxHeight` to override the cube-layer resolution.

## Controls

- Arrow keys move the falling column.
- Up rotates colors forward.
- Q rotates colors backward.
- Hold W/A/S/D to rotate the puzzle grid.
- Hold Page Up/Page Down to zoom the cube view.
- P pauses.
- Escape returns to the menu.

## Build and Run

```bash
cmake -S . -B build
cmake --build build -j --target math3d_masterpiece
./run.pl 3dmath_masterpiece
```
