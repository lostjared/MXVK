# 3D Pool Demo

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/4adfe451-020c-40e9-be78-83da3aa2a397" />

This sample is a full 3D billiards game built with MXVK models, sprites, and physics. It includes an intro screen, a start menu, high scores, and the in-game cue-ball placement and shot logic.

## Controls

### Intro and menus

- `Click` or controller `South` / `Start` - advance from the intro screen
- `Enter` or controller `South` / `Start` - start a new game from the start screen
- `Space` or controller `North` - open high scores from the start screen
- `Escape` - exit from most screens
- `Escape` on the score screen - return to the intro

### In game

- `Left` / `Right` - adjust the cue direction
- Hold `Space` - charge a shot, release to fire
- `Enter` - confirm cue-ball placement when placing the ball
- `R` - reset the table
- `Mouse move` or touch drag - aim
- `Right mouse drag` - rotate the table camera
- `Mouse wheel` or pinch - zoom the camera
- `Press` and `hold` `Click`, touch, or controller `South` / `East` - charge a shot
- `Release` the same input - fire the shot
- Controller `Back` - exit or back out, depending on the screen

### Name entry

- Type text to enter a high-score name
- `Backspace` - delete the last character
- `Enter` - save the score
- `Escape` - cancel name entry

## How It Works

The game simulates a full rack of balls on a table mesh, resolves ball-ball and ball-pocket collisions, animates sunk balls, and keeps a top-10 score file on disk. The camera, cue stick, cue-ball placement, and UI screens are all drawn with MXVK primitives, so the sample acts as a broad integration test for model loading, sprite overlays, text rendering, and input handling.
