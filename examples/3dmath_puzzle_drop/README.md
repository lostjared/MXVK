# 3D Math Puzzle Drop

`3dmath_puzzle_drop` preserves the `puzzle_drop` board, pieces, matching rules,
difficulty levels, and game flow while rendering the board as textured 3D cubes
with the CPU software rasterizer. Vulkan presents the completed framebuffer as a
single sprite. The example uses the normal `mxvk_configure_3dmath_backend`
selection, so it supports either Eigen or the built-in MXVK math implementation.

The background is `puzzle_drop`'s first level image drawn directly, without its
animated fragment shader. The block cubes use the original `puzzle_drop` PNG
textures, and the next-piece panel displays those textures as 2D sprites like
the original example.

## Controls

- Arrow Left/Right: move the falling piece
- Arrow Down: soft drop
- Arrow Up: cycle the three blocks
- Z/X: rotate left/right
- Space: hard drop
- 1/2/3: start at the selected difficulty
- A/D and W/S: rotate the 3D view
- Page Up/Page Down: zoom
- Enter: skip the intro or restart after game over
- Escape: quit
