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
scene and retains the PS1-style presentation. Cube edges use four subpixel
coverage samples to reduce stair-stepping without smoothing the framebuffer or
changing the existing texture treatment. Wildcard blocks use the original
game's rapidly changing neon color effect.

## Rendering quality

Block textures use mipmaps by default. `--mip-bias` adjusts their selected
level-of-detail: negative values such as `--mip-bias -0.75` preserve a sharper
retro texture, while small positive values reduce texture shimmer. Use
`--disable-mipmap` for a fully unmipmapped comparison.

Texture coordinates use perspective-correct interpolation by default. Add
`--nowarpfix` to disable that correction and use optional affine texture
mapping, which produces the characteristic PS1-style texture warping on angled
cube faces.

For a 1280x720 software framebuffer with the sharper mip bias:

```bash
./run.pl 3dmath_puzzle_drop --framebuffer 1280x720 --mip-bias -0.75
```

Add the affine texture effect when desired:

```bash
./run.pl 3dmath_puzzle_drop --framebuffer 1280x720 --mip-bias -0.75 --nowarpfix
```

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
