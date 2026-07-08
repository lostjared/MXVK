# Puzzle Drop

Puzzle Drop is a 3D Acid Drop-style block puzzle. It combines an intro shader, Matrix-rain backdrop, textured cube pieces, selectable difficulty, keyboard controls, and gamepad support.

## Controls

- `Enter` / `Space` - skip the intro or start from menus
- `1` / `2` / `3` - start difficulty levels
- `Left` / `Right` - move the active piece
- `Down` - soft drop
- `Up` - cycle piece blocks
- `Z` / `X` - rotate
- `W` / `A` / `S` / `D` - rotate the board
- `Page Up` / `Page Down` - zoom
- `Escape` - quit

## How It Works

The game renders puzzle blocks as 3D cube models, uses the shared `rain` helper for the Matrix-style background, and compiles its own piece, background, and intro shaders through CMake.

