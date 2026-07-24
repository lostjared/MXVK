# 3D Math Puzzle Drop

`3dmath_puzzle_drop` preserves the `puzzle_drop` board, pieces, matching rules,
difficulty levels, and game flow while rendering the board as textured 3D cubes
with the CPU software rasterizer. Vulkan presents the completed framebuffer as a
single sprite. The example uses the normal `mxvk_configure_3dmath_backend`
selection, so it supports either Eigen or the built-in MXVK math implementation.

The background is `puzzle_drop`'s first level image drawn directly, without its
animated fragment shader. The block cubes use the original `puzzle_drop` PNG
textures. The background, board, status text, and next-piece panel are composed
inside the selected software framebuffer, then displayed together with nearest
filtering so `--framebuffer` consistently controls the resolution of the whole
scene. Wildcard blocks use the original game's rapidly changing neon color
effect.

## Controls

- Arrow Left/Right: move the falling piece
- Arrow Down: soft drop
- Arrow Up: cycle the three blocks
- Z/X: rotate left/right
- 1/2/3: start at the selected difficulty
- A/D and W/S: rotate the 3D view
- Page Up/Page Down: zoom
- Enter/Space: skip the intro
- Enter: restart after game over
- Escape: quit

Gamepad controls match `puzzle_drop`: D-pad/left stick moves and cycles pieces,
the south/west buttons rotate, the east button hard-drops, the right stick
rotates the view, and the shoulder buttons zoom.
