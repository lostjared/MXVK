# MXVK 3D Breakout

MXVK 3D Breakout is a Vulkan and SDL3 version of classic Breakout. A 3D paddle keeps the ball in play while you clear a wall of textured cube blocks. The game uses model rendering for the paddle, ball, intro cube, and blocks, with animated crystal backgrounds and optional SDL3_mixer sound effects when MXVK is built with mixer support.

## How To Play

Start from the intro screen, launch the ball from the paddle, and keep it from falling below the bottom edge.

- Move the paddle under the ball.
- Hit blocks to destroy them and score 10 points each.
- Clear all blocks to return to the intro for a new round.
- You have three tries. Missing the ball three times ends the game.

The HUD shows score at the top center and remaining tries at the upper left.

## Controls

### Start And Game Flow

- `Enter` / `Space` - start from the intro screen or launch the ball
- Left mouse click - start from the intro screen or launch the ball
- Tap - start from the intro screen
- Double tap - launch the ball on touch screens
- `R` - restart the current game
- `Backspace` - return to the intro screen
- `Enter` - leave the game-over screen and return to the intro
- `Escape` - quit

### Paddle

- `Left` / `Right` - move the paddle
- `H` / `L` - move the paddle with alternate keys
- Touch drag - move the paddle horizontally

### View Controls

- `W` / `S` - tilt the board up and down
- `A` / `D` - rotate the board left and right
- `Page Up` / `Page Down` - zoom in and out
- `Q` - reset the board rotation and zoom
- `1` / `2` / `3` - switch to preset board angles

### Gamepad

- `Start` or `South` - start from the intro screen or launch the ball
- Left stick or D-pad left/right - move the paddle
- Right stick - rotate the board
- Left shoulder / Right shoulder - zoom out / in
- `East` - restart the game
- `Back` - return to the intro screen
- `North` - reset the board rotation and zoom

## Building

Build the project from the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

Enable optional audio support with SDL3_mixer:

```bash
cmake -S . -B build -DMIXER=ON
cmake --build build -j
```

## Running

Use the repo helper to launch the example:

```bash
./run.pl breakout
```

You can also pass standard example options such as resolution or fullscreen mode:

```bash
./run.pl breakout -r 1920x1080 -f
```

## Assets

The example ships with its textures, shaders, font, sound effects, manifests, and cube model under `examples/breakout/data/`. The build compiles the Breakout shaders and copies the runtime assets into the example output directory automatically.
