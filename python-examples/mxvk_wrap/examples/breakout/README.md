# mxvk_wrap 3D Breakout

A Python port of the MXVK 3D Breakout sample, using `mxvk_wrap` for window,
sprite, font, and model lifetime management. The game retains the supplied
MXMOD/OBJ artwork and animated backgrounds. The wrapper renders the background
inside the custom 3D callback, before models; the score and life count remain
normal 2D overlays.

From the repository root, first build the extension and generate the local
SPIR-V files:

```sh
cmake -S . -B build-python -DPYTHON_MODULE=ON -DEXAMPLES=OFF
cmake --build build-python --target mxvk_ext -j
python3 python-examples/compile_shaders.py
PYTHONPATH=build-python:python-examples python3 python-examples/mxvk_wrap/examples/breakout/breakout.py
```

Controls: Left/Right or mouse movement moves the paddle; Space, Enter, or a
click launches the ball. Hold W/S to tilt the grid, A/D to rotate it sideways,
and Page Up/Down to zoom. Q resets the view, while 1, 2, and 3 select the native
camera presets. Enter or click starts a game, R restarts during play, and Escape
quits.
