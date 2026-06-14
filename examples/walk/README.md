# Walk

Walk is a first-person maze and exploration sample. It procedurally generates a world of walls, pillars, collectibles, and props, then lets you move through it in a very small FPS-style loop.

## Controls

- `W` / `A` / `S` / `D` - move
- `Mouse` or right stick - look around
- `Left Shift` - sprint
- `Left Ctrl` - crouch
- `Space` - jump
- `Left mouse click` or right shoulder - fire a projectile
- `F` - toggle the FPS overlay
- `Escape` - release mouse capture, or quit if capture is already released
- `Back` / `Start` on a gamepad - quit
- Double-click the left mouse button - recapture the mouse

## How It Works

The example generates a maze, builds the renderable walls and props, and then uses collision checks to keep the player inside the level. Projectiles, explosions, and collectibles are all updated in the main loop, and a built-in debug console exposes commands for spawning, teleporting, regenerating the world, and swapping shaders.

