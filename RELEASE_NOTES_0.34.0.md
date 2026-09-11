# MXVK 0.34.0 Release Notes

Prepared: September 11, 2026

MXVK 0.34.0 adds an optional Python surface powered by nanobind. The native
C++ library remains the primary API and its normal CMake configuration does not
require Python. Enable the extension with `-DPYTHON_MODULE=ON`, or install the
wheel with `python -m pip install .`.

## Highlights

- `mxvk_ext` provides Python access to windows, input, sprites, text, models,
  post-processing, frame readback, PNG helpers, joystick support, and safe
  runtime configuration.
- Python classes can directly subclass `mxvk_ext.VK_Window` and implement
  `proc()`, `event(event)`, `on_swapchain_recreated()`, and
  `on_record_custom_rendering(command_buffer, image_index)`.
- `AbstractModel` and `ModelUniforms` expose model loading, shaders, resize,
  uniform updates, rendering, and explicit cleanup. The command buffer from
  `on_record_custom_rendering()` is accepted directly by `AbstractModel.render()`.
- `wait_idle()` supports an explicit safe teardown sequence: wait for Vulkan,
  clean up Python-owned models, then call `release()`.
- Native `mxvk::Exception` values are translated to `mxvk_ext.MXVKError`.
- The wheel packages shared SPIR-V shaders and the module discovers them from
  its installation directory. Examples can select local shader bundles with
  `set_default_shader_directory()`.

## Building and installing

Build the extension from a CMake tree without enabling native examples:

```bash
cmake -S . -B build-python -DPYTHON_MODULE=ON -DEXAMPLES=OFF
cmake --build build-python -j
PYTHONPATH=build-python python3 python-examples/window/main.py
```

For a virtual environment or system Python installation:

```bash
python3 -m pip install .
```

The pip package is named `mxvk`; import its compiled extension as `mxvk_ext`.

## Python API notes

`Window`/`VK_Window` and `IOWindow`/`VK_IOWindow` are aliases. The direct
`VK_Window` form is appropriate for Python subclasses. Events expose key,
modifier, mouse button, motion, wheel, and text-input fields; `key_code()` and
`key_name()` convert SDL key names and codes.

For custom models, implement `on_record_custom_rendering()` and use the supplied
command buffer for `AbstractModel.render()`. Recreate model swapchain resources
in `on_swapchain_recreated()`. This matches the recording lifetime of native
`VK_Window` subclasses and keeps custom model draws inside MXVK's dynamic
rendering pass.

The `python-examples/` tree contains runnable, bundled examples for windows,
sprites, Asteroids, Knight's Tour, and the Darkside model scene. Their `data/`
directories include the assets and SPIR-V shaders they require.
