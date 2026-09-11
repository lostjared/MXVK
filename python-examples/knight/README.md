# Python Knight's Tour

This is a self-contained Python port of `examples/knight`. Its `data/`
directory contains the knight and logo images, font, source shaders, and all
compiled sprite and text shaders required at runtime. The script selects this
bundled shader directory before creating the window, so it does not rely on
system-wide shader files.

Run it from any working directory after installing `mxvk`:

```bash
python3 python-examples/knight/knight.py
```

Space advances the tour, Return or right-click restarts it, left-click starts
a tour from a selected board square, S saves `screenshot.png`, and Escape
closes the window. `--path` overrides the resource root when needed.
