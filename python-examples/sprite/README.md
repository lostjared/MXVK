# Sprite example

This is the Python equivalent of `examples/sprite_example`. It loads the same
image, font, and shaders, stretches the sprite to the current window size, and
draws `Hello, World!` over it. Press Escape or close the window to exit.

Build the optional Python module from the repository root:

```bash
cmake -S . -B build_python -DPYTHON_MODULE=ON -DEXAMPLES=OFF
cmake --build build_python -j
```

Then run the example using the directory containing `mxvk_ext`:

```bash
PYTHONPATH=build_python python3 python-examples/sprite/main.py
```

Optional arguments are `--width`, `--height`, `--fullscreen`, and `--vsync`.

The example explicitly drops its Python sprite handle before releasing the
window. MXVK owns sprites created by a window, while nanobind keeps their owner
alive to prevent dangling native pointers. Python subclasses that store other
window-owned handles on `self` should clear those attributes during shutdown in
the same order.

Use `mxvk.key_code()` to handle any SDL key by name. For example, a Python
`console_event` implementation can check
`event.key_down and event.key == mxvk.key_code("Space")`. `EVENT_KEY_UP` and
the `MOD_SHIFT`, `MOD_CTRL`, `MOD_ALT`, and `MOD_GUI` modifier masks are also
available.

Mouse input is delivered through the same callback. Check `event.mouse_motion`,
`event.mouse_button_down`, `event.mouse_button_up`, or `event.mouse_wheel`.
Mouse coordinates are `event.x` and `event.y`; motion deltas are
`event.relative_x` and `event.relative_y`; wheel deltas are `event.wheel_x` and
`event.wheel_y`. Compare `event.button` with `MOUSE_BUTTON_LEFT`,
`MOUSE_BUTTON_MIDDLE`, `MOUSE_BUTTON_RIGHT`, `MOUSE_BUTTON_X1`, or
`MOUSE_BUTTON_X2`.

To rebuild the copied shaders after editing their sources:

```bash
glslc python-examples/sprite/shaders/vertex.vert -o python-examples/sprite/data/sprite.vert.spv
glslc python-examples/sprite/shaders/fragment.frag -o python-examples/sprite/data/fragment.frag.spv
```
