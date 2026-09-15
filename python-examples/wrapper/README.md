# Simple MXVK wrappers

This folder is a small, pure-Python façade over `mxvk_ext`. It covers the main
nanobind modules with friendly application, drawing, model, input, settings,
media, timer, and GPU-resource classes. Every wrapper keeps the raw binding at
`.native` when an advanced MXVK feature is needed. The full usage guide is in
the root [Simple Python Wrappers](../../README.md#simple-python-wrappers)
documentation section.

Run the example from the repository root after building the extension:

```sh
PYTHONPATH=build-python:python-examples python3 python-examples/wrapper/example.py
```

`App` owns cleanup. Create graphics resources with the app, implement `draw`,
and call `run()`. `example.py` is the smallest complete reference.

For one-off text styles, use `Font(path, size)` with
`app.draw_text(text, x, y, font, color)`. This does not modify the font selected
by `app.set_font()`.
