# Mutatris

<img width="1280" height="720" alt="vlcsnap-2026-07-03-20h52m25s796" src="https://github.com/user-attachments/assets/60e1cc26-32de-4160-96ec-c25155c3e23d" />

Mutatris is a four-sided falling-block puzzle game built with MXVK, Vulkan, and SDL3. Three-block pieces enter one side of the playfield at a time, then the active side rotates around the board after a piece locks. The game includes startup logos, a title screen, Easy/Medium/Hard difficulty selection, animated backgrounds with runtime shader effects, optional music and sound effects, and game-over or high-score presentation screens.

## Gameplay

Mutatris plays like a falling-block color matcher wrapped around four separate grids. Only one side is active at a time. Place the current three-block piece, let the board resolve matches and gravity, then the active side advances clockwise to the next grid.

### Board Grids

The board is split into four `GameGrid` instances that share one visual playfield:

| Grid | Logical Size | Screen Placement | Fall Direction |
|------|--------------|------------------|----------------|
| Top | 24 x 23 cells | centered along the top edge | downward toward the middle |
| Right | 25 x 28 cells | right side of the screen | leftward toward the middle |
| Bottom | 24 x 22 cells | centered along the bottom edge | upward toward the middle |
| Left | 25 x 28 cells | left side of the screen | rightward toward the middle |

The side grids use the same underlying cell storage as the top and bottom grids, but they are drawn rotated into the left and right sides of the board. This lets pieces move with the same local grid rules while appearing to fall inward from each edge. The side grids are one column wider than the top and bottom grids so their visible rows line up cleanly with the central playfield.

### Piece Flow

- Each grid owns its own active three-block piece.
- A new game starts on the top grid.
- After a piece locks, focus advances `Top -> Right -> Bottom -> Left -> Top`.
- The highlighted frame shows which grid currently accepts input.
- Arrow movement is relative to that grid, so "soft drop" always pushes the piece inward even though the physical key changes by side.
- `W` or the side-specific color-cycle arrow rotates the three colors within the piece.
- `A` or `Space` rotates the piece shape through vertical and horizontal orientations when the target cells are empty.
- `S` hard drops the active piece until it locks.

### Clearing And Gravity

After a piece locks, Mutatris resolves the board in small steps:

- Runs of three or more matching colors clear horizontally, vertically, and diagonally inside each grid.
- Visual matches can also cross between grids when adjacent cells line up on screen, so a run can continue across the top/bottom divider or through aligned neighboring board cells.
- Dedicated top/bottom seam checks catch matches that span the center divider. A four-block seam match gets an extra score bonus.
- Clearing blocks play a 400 ms twist animation before their cells become empty.
- Once cells are empty, blocks above them fall through their own grid until they settle.
- Cascades are processed over repeated frames, so a clear can create more falling blocks and follow-up clears.

### Difficulty, Level, And Game Over

Easy, Medium, and Hard start with different automatic drop timeouts: 1200 ms, 900 ms, and 650 ms. Every eight clears reduces the timeout by 100 ms until it reaches the 125 ms minimum, and the level display is derived from the current timeout.

The game ends when the active grid can no longer accept the falling piece at its entry position. The HUD shows the current level, drop timeout, score, active direction index, and a short controls reminder.

## Controls

### Menus

- Any key, mouse click, tap, `A` / `South`, or `Start` - skip startup logos
- `Enter` / `Space` - advance from title to difficulty selection
- Mouse click, tap, `A` / `South`, or `Start` - confirm the current menu screen
- `Left` / `Right` - choose Easy, Medium, or Hard on the difficulty screen
- `Enter` / `Space` - start the selected difficulty
- `Escape` - quit

### In Game

- Arrow keys - move or soft-drop relative to the active side of the board
- `W` - cycle the active piece's colors
- `A` / `Space` - rotate the active piece
- `S` - hard drop the active piece
- `F3` - open or close the developer console
- `F8` - toggle the CRT post-processing shader on or off
- `Escape` - quit

Because the active side rotates after each locked piece, the arrow mapping changes with the highlighted side:

| Active Side | Move Left | Move Right | Cycle Colors | Soft Drop |
|-------------|-----------|------------|--------------|-----------|
| Top | `Left` | `Right` | `Up` | `Down` |
| Left | `Up` | `Down` | `Left` | `Right` |
| Bottom | `Left` | `Right` | `Down` | `Up` |
| Right | `Down` | `Up` | `Right` | `Left` |

### Gamepad

- `A` / `South` or `Start` - advance menus or start the selected difficulty
- D-pad left/right - choose difficulty on the difficulty screen
- D-pad - move or soft-drop relative to the active side
- `A` / `South` - cycle colors
- `B` / `East` - rotate the active piece
- `North` - hard drop the active piece
- `Back` - quit

### Console

Press `F3` to toggle the in-window console. Available commands include:

- `help` - list commands
- `clear` - clear console output
- `echo <text>` - print text
- `switch_shader` - choose another random background and shader effect
- `about` - show app information
- `quit` / `exit` - close Mutatris

## Visuals And Audio

Mutatris builds a shader effect pack from `examples/mutatris/shaders/effects/`. CMake compiles each `.glsl` file to SPIR-V under the example build directory, and the game randomly picks a new background/effect combination when the level changes. The console `switch_shader` command can force a new random selection while the game is running.

Audio is optional. Build the repository with SDL3_mixer enabled to play the bundled background music and sound effects:

```bash
cmake -S . -B build -DMIXER=ON
cmake --build build -j
```

When mixer support is enabled, `music.ogg` loops during play and `line.wav` / `open.wav` are loaded as sound effects. Without `-DMIXER=ON`, the game still builds and runs silently.

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

Use `--enable-crt` to start with the CRT post-processing shader enabled:

```bash
./run.pl mutatris --enable-crt
```

## Assets

The game ships with textures, font data, sound files, and custom fragment shaders under `examples/mutatris/data/` and `examples/mutatris/shaders/`. CMake compiles the shaders, including the effect shaders under `shaders/effects/`, and copies the runtime assets into the example output directory automatically.
