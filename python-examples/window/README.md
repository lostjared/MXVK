# Python window example

Build the optional nanobind module from the repository root:

```bash
cmake -S . -B build -DPYTHON_MODULE=ON
cmake --build build --target mxvk_ext -j
```

Run the example with the build directory on Python's module path:

```bash
PYTHONPATH="$PWD/build" python3 python-examples/window/main.py
```

Close the window normally to leave the native MXVK event loop.
