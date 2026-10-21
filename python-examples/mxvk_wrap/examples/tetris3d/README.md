# mxvk_wrap 3D Tetris

Python port of `examples/tetris`, using `mxvk_wrap.App`, `Model`, `Sprite`,
`Font`, `Controller`, and optional `Sound`. The native cube models, textures,
background artwork, Tux credits model, music, and GLSL shaders are bundled
locally. No runtime paths point into the C++ example.

Includes the intro, main menu, game, next-piece preview, high scores, credits,
game-over name entry, and TCP multiplayer with an opponent preview. The board
is the native 10×20 Tetris board displayed as lit 3D cubes, with a rotatable
camera. Each visible cube has independent model uniforms. Models are reused
through per-color pools, and resized when the swapchain changes.

Build the Python extension from the MXVK repository root (use the same Python
interpreter for building and running):

```sh
cmake -S . -B build_python -DPYTHON_MODULE=ON -DEXAMPLES=OFF
cmake --build build_python --target mxvk_ext -j
PYTHONPATH=build_python:python-examples python3 python-examples/mxvk_wrap/examples/tetris3d/main.py
```

Or, from this `examples` directory:

```sh
PYTHONPATH=../../../build_python:../.. python3 tetris3d/main.py
```

Module launch is also supported: `python3 -m mxvk_wrap.examples.tetris3d`.
Use `--help` for options, including `-r 1920x1080`, `-f`, `--mute`,
`--validation`, `--start` (skip the intro/menu), and `--frames N` (exit after
N frames). Music plays when the extension is built with SDL3_mixer support;
otherwise the game runs silently. NumPy is required for the model matrices.

## Controls

| Screen | Controls |
| --- | --- |
| Intro | Enter / Space to skip |
| Menu | Up / Down to choose, Enter to select, Escape to quit |
| Game | Left / Right to move, Down to soft drop, Up to rotate, Z / Space to hard drop |
| Camera | W / S pitch, A / D yaw, Q / E roll, Page Up / Page Down zoom |
| Restart | R returns to the intro; Escape returns to the menu |
| Name entry | Up / Down choose character, Space append, Backspace delete, Enter save, Escape skip |
| Game over | Enter restart, H high scores, Escape menu |
| Scores / Credits | Enter / Escape returns to the menu |

Gamepads: left stick or D-pad moves/drops, South rotates or confirms, East
hard-drops or goes back, Back returns to the menu, right stick tilts the
camera, and shoulders zoom. During name entry, South appends, East deletes,
West saves, and D-pad Up / Down selects the character. Controllers are
rediscovered periodically after a disconnect.

The native scoring is preserved: each cleared row earns 10 points, with a
5-point bonus for clearing multiple rows at once. Drops award no points.
Rows blink for one second before collapsing; gravity advances every level,
with a new level every eight lines. Gravity and input repeat use elapsed time.
The camera starts slightly farther back to leave room for the controls HUD.
Locking a piece above the board ends the game instead of discarding cells.

## Multiplayer and scores

Choose Network Multiplayer. Press H on the host; on the guest, edit the IPv4
address with Backspace and digits, then press J / Enter. Both games start when
the TCP connection completes. The port is **37373**. The snapshot wire format
matches the native C++ example; the automated regression test exchanges
snapshots between two Python peers. Cross-version play with a running C++
peer has not been exercised.

The top ten scores are saved to `tetris3d/scores.json` by default. Use
`--scores /path/to/scores.json` to choose another location. This is a separate
JSON score file, not the native game's binary score file. Save failures are
shown on screen and allow retrying.

## Shaders and checks

Compiled SPIR-V is included. If you edit GLSL, regenerate with:

```sh
python3 tetris3d/compile_shaders.py
```

Startup also recompiles missing or stale shaders when `glslc` is available.
The local compiler handles this port independently of the repository's older
Python shader manifest.

From the `examples` directory:

```sh
python3 -m unittest tetris3d.test_game -v
PYTHONPATH=../../../build_python:../.. python3 tetris3d/main.py --start --mute --validation --frames 120
```

The rule, score-file, and TCP tests need no Vulkan window or extension. The
TCP test needs permission to create localhost sockets. The smoke test needs
a working SDL3/Vulkan display. Screenshots in `screenshots/` were captured
from the port with Vulkan validation enabled.
