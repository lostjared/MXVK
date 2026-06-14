# MXVK 3D Tetris

<img width="2560" height="1600" alt="image" src="https://github.com/user-attachments/assets/5ff70936-76ad-41cc-ae20-be40adcba68d" />

<img width="3360" height="2100" alt="image" src="https://github.com/user-attachments/assets/10c198a5-6308-4dbb-8c53-c31c02e33c45" />

<img width="3840" height="2160" alt="vlcsnap-2026-06-14-01h47m52s174" src="https://github.com/user-attachments/assets/005df8b6-7c2c-4a15-8d32-c9373a021f5f" />



MXVK 3D Tetris is a Vulkan and SDL3 take on classic Tetris. Pieces fall into a 3D playfield, and you score points by moving, rotating, and dropping them to complete lines. The game includes a title screen, high-score table, credits, a game-over flow for saving your name, and a simple network multiplayer mode.

## How To Play

The goal is to keep the board clear for as long as possible.

- Move falling pieces into position.
- Rotate them to fit gaps.
- Clear lines to score points and advance levels.
- If the stack reaches the top, the game ends.

The HUD shows your current score, cleared lines, and level while you play.

## Controls

### Menu

- `Up` / `Down` - move through the menu
- `Enter` - select the highlighted item
- `Escape` - quit from the main menu, or return to the menu from other screens

### In Game

- `Left` / `Right` - move the active piece
- `Down` - soft drop
- `Up` - rotate the piece
- `Z` - hard drop
- `R` - restart the game from the intro sequence
- `Escape` - return to the menu

### View Controls

These controls rotate and zoom the 3D camera around the board:

- `W` / `S` - tilt the board up and down
- `A` / `D` - rotate the board left and right
- `Q` / `E` - roll the view
- `Page Up` / `Page Down` - zoom in and out

### Game Over Name Entry

When you get a high score, the game asks for a name:

- `Up` / `Down` - cycle the current character
- `Space` - append the selected character
- `Backspace` - delete the last character
- `Enter` - save the score
- `Escape` - skip saving and return to the menu

### Gamepad

The game also supports a gamepad:

- Left stick - move the piece
- Left stick down - soft drop
- Right stick - rotate the 3D view
- Left / Right shoulder - zoom out / in
- `South` button - rotate or confirm, depending on the screen
- `East` button - hard drop in game, or back out of screens
- `Back` button - return to the menu

## Screens And Modes

- `New Game` - start the standard single-player game
- `Network Multiplayer` - host or join a peer over the network
- `High Scores` - view the saved score table
- `Credits` - show the credits screen

### Network Multiplayer

The multiplayer mode uses a fixed port: `37373`.

- Press `H` to host a session.
- Enter the peer IP address, then press `J` or `Enter` to join.
- Use `Backspace` to edit the IP address.
- Press `Escape` to leave multiplayer and return to the menu.

Run one copy as the host, then connect to it from the other machine using its IP address.

## Building

Build the project from the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

## Running

Use the repo helper to launch the example:

```bash
./run.pl tetris
```

You can also pass standard example options such as resolution or fullscreen mode:

```bash
./run.pl tetris -r 1920x1080 -f
```

## Assets

The example ships with its own textures, shaders, music, and model data under `examples/tetris/data/`. The build copies the runtime assets into the example output directory automatically.

