# Viewer

`viewer` is the command-line MXVK model inspection renderer. It is built as part of the normal examples build and is intended for quickly loading `.obj`, `.mxmod`, and `.mxmod.z` assets without the extra UI flow used by the Qt `model_viewer` launcher.

The program renders through `mxvk::VKAbstractModel`, uses the shaders in this directory, and provides a small orbit camera with wireframe and auto-rotation controls. It is also the renderer process launched by the top-level `model_viewer` Qt application.

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

The target name is `viewer`, and its compiled shaders are generated under the example build directory.

## Usage

Run it through the repository helper so the built executable and asset paths are resolved consistently:

```bash
./run.pl viewer
./run.pl viewer --filename ./models/moon.mxmod.z
./run.pl viewer --filename ./models/obj/cube.obj --texture ./models/obj/cube.mtl --resource_path ./models/obj
```

When `--filename` is omitted, the viewer looks for the default `cube.mxmod.z` model.

## Arguments

- `--filename <file>` - model file to load. Supported formats include `.obj`, `.mxmod`, and `.mxmod.z`.
- `--texture <file>` - optional texture manifest or material file passed to the model loader.
- `--resource_path <dir>` - optional base directory for resolving textures referenced by the manifest or model.
- `--shader-path <dir>` - optional directory containing `model.vert.spv` and `model.frag.spv`; defaults to the compiled viewer shader output.
- `-p <path>` - asset root. `run.pl` passes this automatically.
- `-r <WxH>` - window resolution.
- `-f` - fullscreen.
- `--enable-vsync` - request FIFO present mode.

## Controls

- `Escape` - quit.
- `Left mouse drag` - rotate the model.
- `Arrow keys` - rotate the model.
- `Mouse wheel`, `+`, `-`, `A`, `S` - zoom in or out.
- `W` - toggle wireframe rendering.
- `R` or `P` - toggle auto-rotation.
- `H` or `Space` - show or hide the help overlay.
- `Home` - reset rotation and camera distance.

## Purpose

Use `viewer` when you need a direct Vulkan model-rendering smoke test, a simple model inspection tool, or a backend process for another launcher. Use `model_viewer` when you want a desktop GUI for choosing files, managing recent models, selecting resolution presets, and reading renderer output.
