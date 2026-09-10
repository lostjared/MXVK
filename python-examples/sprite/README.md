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

To rebuild the copied shaders after editing their sources:

```bash
glslc python-examples/sprite/shaders/vertex.vert -o python-examples/sprite/data/sprite.vert.spv
glslc python-examples/sprite/shaders/fragment.frag -o python-examples/sprite/data/fragment.frag.spv
```
