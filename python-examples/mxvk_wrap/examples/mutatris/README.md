# Mutatris for mxvk_wrap

Python translation of `examples/mutatris`, using `mxvk_wrap` for its Vulkan
window, sprites, fonts, controller, and optional mixer audio. Bundles the original
artwork, music, sounds, font, and all 77 background effect shaders.

From this directory (`python-examples/mxvk_wrap/examples`), with the current
native extension in the repository's `build` directory:

```sh
PYTHONPATH=../../../build python3 mutatris/main.py
```

The installed extension also works without `PYTHONPATH`. To build a current
extension from the repository root:

```sh
cmake -S . -B build -DPYTHON_MODULE=ON -DEXAMPLES=OFF -DMIXER=ON
cmake --build build --target mxvk_ext -j
PYTHONPATH=build python3 python-examples/mxvk_wrap/examples/mutatris/main.py
```

Omit `-DMIXER=ON` for silent play. Requires the existing wrapper dependencies
and Vulkan/SDL3 runtime. The first launch compiles the bundled shaders with
`glslc`; subsequent launches compile only changed or missing shaders. You can
also prepare them with `python3 mutatris/compile_shaders.py`.

Options: `-r 1920x1080`, `-f` / `--fullscreen`, `--no-vsync`, `--enable-crt`,
and `--validation`. Launching as a module is also supported:
`PYTHONPATH=../../../build python3 -m mutatris`.

The port retains the original startup fades, title and difficulty screens,
24×23 top / 24×22 bottom / 25×28 side grids, clockwise focus changes, seven
colors, four piece orientations, diagonal and cross-grid matching, seam bonus,
400 ms clear animation, gradual gravity, cascades, difficulty timeouts, level
progression, and game-over/high-score screens. A blocked piece placement is
validated before writing cells so an unsuccessful lock cannot partially modify
the board. Gameplay processing follows the original four passes per rendered
frame; automatic drops and clear animations use elapsed time.

Enter/Space or a click/tap advances menus. Left/Right selects difficulty.
During play, W cycles colors, A/Space rotates, and S hard drops. Arrow controls
follow the current side:

| Side | Move left | Move right | Cycle colors | Soft drop |
|------|-----------|------------|--------------|-----------|
| Top | Left | Right | Up | Down |
| Right | Down | Up | Right | Left |
| Bottom | Left | Right | Down | Up |
| Left | Up | Down | Left | Right |

Escape returns from play to difficulty selection; elsewhere it quits. F8 toggles
CRT. F3 toggles the console, with `help`, `clear`, `echo <text>`, `switch_shader`,
`about`, `quit`, and `exit`; Up/Down recalls command history.

Gamepad South/A or Start confirms menus; D-pad Left/Right selects difficulty.
During play, D-pad moves/drops relative to the active side, South/A cycles colors,
East/B rotates, North/Y hard drops, and Back quits. Controllers are discovered
at startup and after connection changes. Console text handling uses Python
because the native console does not expose its event handler or command callback.

Run the rules tests without Vulkan, a window, or the native extension:

```sh
python3 -m unittest discover -s mutatris -v
```
