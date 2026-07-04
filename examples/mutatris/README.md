# Mutatris

Mutatris is a four-sided falling-block puzzle game built with MXVK, Vulkan, and SDL3. Three-block pieces enter one side of the playfield at a time, then the active side rotates around the board after a piece locks. The game includes startup logos, a title screen, Easy/Medium/Hard difficulty selection, animated backgrounds, and game-over or high-score presentation screens.

## How To Play

Keep the board clear by placing pieces and matching runs of the same color.

- Each falling piece contains three colored blocks.
- Move the active piece within the currently highlighted side of the board.
- Rotate the piece or cycle its block colors before it locks.
- Match three or more equal-colored blocks horizontally, vertically, diagonally, or across the top/bottom seam to clear them.
- The game speeds up as you clear blocks.
- If the active side can no longer accept the falling piece, the game ends.

The HUD shows the current level, drop timeout, score, active direction, and a short controls reminder.

## Controls

### Menus

- Any key, mouse click, tap, `South`, or `Start` - skip startup logos
- `Enter` / `Space` - advance from title to difficulty selection
- Mouse click, tap, `South`, or `Start` - confirm the current menu screen
- `Left` / `Right` - choose Easy, Medium, or Hard on the difficulty screen
- `Enter` / `Space` - start the selected difficulty
- `Escape` - quit

### In Game

- Arrow keys - move or soft-drop relative to the active side of the board
- `W` - cycle the active piece's colors
- `A` / `Space` - rotate the active piece
- `S` - hard drop the active piece
- `Escape` - quit

Because the active side rotates after each locked piece, the arrow mapping changes with the highlighted side:

| Active Side | Move Left | Move Right | Cycle Colors | Soft Drop |
|-------------|-----------|------------|--------------|-----------|
| Top | `Left` | `Right` | `Up` | `Down` |
| Left | `Up` | `Down` | `Left` | `Right` |
| Bottom | `Left` | `Right` | `Down` | `Up` |
| Right | `Down` | `Up` | `Right` | `Left` |

### Gamepad

- `Start` / `South` - advance menus or start the selected difficulty
- D-pad left/right - choose difficulty on the difficulty screen
- D-pad - move, soft-drop, or cycle colors relative to the active side
- `South` - cycle colors
- `East` - rotate the active piece
- `North` - hard drop the active piece
- `Back` - quit

## Building

Build the project from the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

## Running

Use the repo helper to launch the example:

```bash
./run.pl mutatris
```

Standard example options work as well:

```bash
./run.pl mutatris -r 1920x1080 -f
```

## Assets

The game ships with textures, font data, sound files, and custom fragment shaders under `examples/mutatris/data/` and `examples/mutatris/shaders/`. CMake compiles the shaders and copies the runtime assets into the example output directory automatically.
