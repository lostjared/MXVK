# Defender

Defender is a side-scrolling 3D starfield shooter with a model-based ship and asteroids, animated UFO sprites, radar/HUD, controller support, an in-game MXVK console, a Matrix-rain intro, and optional SDL3_mixer audio.

## Inputs

- `-p <path>` or `--path <path>` - asset root, usually handled automatically by `run.pl`
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode

## Controls

- `Enter` or `Space` - skip the intro or restart from game over
- `Z` - thrust
- `D` - boost
- `X` - reverse direction
- `W` / `Up` - move up
- `Down` - move down
- `A` / `S` - roll left or right
- `Space` - fire while playing
- `F3` - open or close the in-game console
- `F4` - toggle the FPS counter
- `F8` - toggle the CRT post-processing shader on or off
- `Escape` - quit

## How It Works

The sample combines a 3D ship model, asteroid models, animated 3D sprites for UFOs and effects, a scrolling starfield, HUD text, and a scanner overlay. It renders game content through the normal MXVK scene path, then uses `mxvk::VK_Window` post-processing for the CRT effect.

The CRT effect is enabled by default in Defender. Press `F8` at runtime to disable or re-enable the final full-screen shader pass.
