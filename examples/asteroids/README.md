# Space Rox

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/f58e3c2e-3b84-41af-b5f8-432e3eb48e31" />

Space Rox is a small Asteroids-style arcade shooter built on the MXVK window and sprite APIs. It uses a fixed 640x360 playfield, draws the ship and asteroids with software-style line rasterization, and drives the whole game from a simple state machine with countdown, launch, play, and game-over phases.

## Controls

- `Left` / `Right` - rotate the ship
- `Up` - thrust forward
- `Space` - fire projectiles
- `Space` on the game-over screen - restart
- `Escape` - quit

## How It Works

The example updates ship physics, projectile motion, asteroid spawning, collisions, and scoring every frame, then renders the world through MXVK text and sprite overlays. Stars, particles, and the launch countdown are all drawn in code rather than coming from prebuilt UI assets.

