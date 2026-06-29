# Asteroids3D

Asteroids3D is a 3D take on the classic Asteroids formula. You fly a ship through a populated volume, shoot asteroids, manage speed, and can inspect the game through a built-in MXVK console.

## Inputs

- `-p <path>` or `--path <path>` - asset root, usually handled automatically by `run.pl`
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode
- `--enable-crt` - start with the CRT post-processing shader enabled

## Controls

- `Escape` - return to the intro screen from play, or quit from non-play screens
- `Space` or `Enter` - skip the intro and start a new game
- `F1` - toggle the debug HUD while playing
- `F2` - toggle arcade versus inverted pitch controls
- `F3` - open or close the in-game console
- `F5` - toggle classic keyboard controls versus keyboard/mouse controls
- `F7` - toggle chase camera versus first-person camera while playing
- `F8` - toggle the CRT post-processing shader on or off
- `Left` / `Right` - yaw the ship with the keyboard
- `W` / `S` - pitch the ship, with inversion controlled by `F2`
- `A` / `D` - roll the ship manually
- `Up` / `Down` - adjust ship speed
- Keyboard/mouse mode: mouse looks around, `W` / `S` adjust speed, `A` / `D` roll, left click fires
- `Space` - fire projectiles while playing
- Gamepad `South` - start from the intro or fire while playing
- Gamepad `West` - toggle the debug HUD
- Gamepad `North` - toggle arcade versus inverted controls
- Gamepad `Back` - quit
- Gamepad `Left shoulder` / `D-pad Up` - increase speed
- Gamepad `Right shoulder` / `D-pad Down` - decrease speed
- Gamepad `Left stick` - yaw
- Gamepad `Right stick` - roll and pitch

## How It Works

The sample combines a ship model, multiple asteroid meshes, projectiles, particle effects, and a starfield. It updates movement, collision detection, scoring, and spawning in the main loop, then renders the scene with separate model and sprite passes.

The CRT effect is attached through `mxvk::VK_Window`'s post-processing path and is off by default unless `--enable-crt` is provided. Press `F8` at runtime to enable or disable the final full-screen shader pass.

The built-in console exposes commands for restarting, toggling the HUD, switching controls, and quitting, which makes the example useful both as a gameplay demo and as a reference for in-engine debugging.
