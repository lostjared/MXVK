# mxvk_wrap 2D Tetris

A compact, playable 2D Tetris clone written with `mxvk_wrap`. It uses the
block, background, and font files in this directory's `data/` folder, and a
`Font` passed to `App.draw_text()` for the HUD without changing the window font.
Completed rows flash with the supplied gray block texture before collapsing.
Gravity and the clear animation run on a fixed 60 Hz simulation step, so their
timing does not depend on the rendering frame rate. The HUD also shows the next
tetromino.

From the repository root, build the MXVK Python extension:

```sh
cmake -S . -B build-python -DPYTHON_MODULE=ON -DEXAMPLES=OFF
cmake --build build-python --target mxvk_ext -j
PYTHONPATH=build-python:python-examples python3 python-examples/mxvk_wrap/examples/tetris/main.py
```

Controls: Left/Right to move, Up to rotate, Down to soft drop, Space to hard
drop, R to restart after game over, and Escape to quit.
