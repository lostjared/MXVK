# Mutatris

<img width="1280" height="720" alt="vlcsnap-2026-07-03-20h52m25s796" src="https://github.com/user-attachments/assets/60e1cc26-32de-4160-96ec-c25155c3e23d" />

Mutatris is a four-sided falling-block puzzle game built with MXVK, Vulkan, and SDL3. Three-block pieces enter one side of the playfield at a time, then the active side rotates around the board after a piece locks. The game includes startup logos, a title screen, Easy/Medium/Hard difficulty selection, animated backgrounds with runtime shader effects, optional music and sound effects, and game-over or high-score presentation screens.

## How To Play

Keep the board clear by placing pieces and matching runs of the same color.

- Each falling piece contains three colored blocks.
- Move the active piece within the currently highlighted side of the board.
- Rotate the piece or cycle its block colors before it locks.
- Match three or more equal-colored blocks horizontally, vertically, diagonally, or across the top/bottom seam to clear them.
- Matches are checked against the board's visual layout, so runs can continue across the central top/bottom divider when the cells line up on screen.
- Clearing blocks play a fixed-duration twist animation before the cells disappear.
- The game speeds up as you clear blocks.
- If the active side can no longer accept the falling piece, the game ends.

The HUD shows the current level, drop timeout, score, active direction, and a short controls reminder.

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
