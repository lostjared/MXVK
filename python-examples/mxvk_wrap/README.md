# mxvk_wrap: simple MXVK wrappers

This folder is a small, pure-Python façade over `mxvk_ext`. It covers the main
nanobind modules with friendly application, drawing, model, input, settings,
media, timer, and GPU-resource classes. Every wrapper keeps the raw binding at
`.native` when an advanced MXVK feature is needed. The full usage guide is in
the root [Simple Python Wrappers](../../README.md#simple-python-wrappers)
documentation section.

Run the example from the repository root after building the extension:

```sh
PYTHONPATH=build-python:python-examples python3 python-examples/mxvk_wrap/example.py
```

`App` owns cleanup. Create graphics resources with the app, implement `draw`,
and call `run()`. `example.py` is the smallest complete reference.

Use the package as `import mxvk_wrap as mx`, then construct values such as
`mx.App`, `mx.Sprite`, and `mx.Font`. For one-off text styles, use
`mx.Font(path, size)` with `app.draw_text(text, x, y, font, color)`. This does
not modify the font selected by `app.set_font()`.

If an app-owned `Sprite`, `Sprite3D`, `Model`, `GpuBuffer`, or `GpuTexture`
fails to load or allocate, its wrapper closes the owning `App` before raising
the original exception. This releases resources that were created earlier in
the same application setup.

## Included examples

- [2D Tetris](examples/tetris/README.md) is a playable sprite-and-font game
  using the local artwork in `examples/tetris/data/`.
