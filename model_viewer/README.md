# MXMOD Model Viewer

A Qt6-based GUI front end for viewing 3D mesh and model files with the MXVK **viewer** application.

<img width="1923" height="1206" alt="Screenshot" src="https://github.com/user-attachments/assets/5f410902-a5c5-4eca-ba6f-1fdd45c0e13c" />

## Overview

MXMOD Model Viewer provides a desktop application for loading, inspecting, and rendering 3D models stored in `.mxmod`, `.mxmod.z` (compressed), and `.obj` formats. The viewer launches the MXVK `viewer` renderer and displays its output in a built-in console.

## Features

- **MXVK viewer backend** — launches the repository `viewer` executable
- **Supported model formats** — `.mxmod`, `.mxmod.z` (zlib-compressed), and Wavefront `.obj`
- **Texture support** — optionally load `.tex`, `.png`, `.jpg`, `.bmp`, and `.mtl` texture files; texture directory can override relative lookup paths
- **Resolution presets** — 720p, 1080p, 1440p, and 4K launch sizes
- **Drag and drop** — drop model or texture files directly onto the window
- **Recent files** — quick access to previously opened models
- **Real-time console** — timestamped stdout/stderr from the renderer with color-coded errors
- **Persistent settings** — window size, position, paths, and preferences are saved between sessions
- **Custom stylesheet** — ships with a dark theme via `stylesheet.qss`

## Prerequisites

- **Qt 6** (Core, Gui, Widgets)
- **CMake 3.16+**
- **C++20** compatible compiler
- The MXVK `viewer` executable built from this repository, available next to `model_viewer` or on your `PATH`

## Building

```bash
mkdir build && cd build
cmake ..
make
```

The build will automatically copy the `data/` directory (models, textures, shaders, fonts) alongside the executable.

Alternatively, open `model_viewer.pro` in Qt Creator for a qmake-based workflow.

## Usage

1. Launch the `model_viewer` executable.
2. Select a **Model File** (`.mxmod`, `.mxmod.z`, or `.obj`).
3. Optionally select a **Texture File** (`.tex`, `.png`, `.mtl`, etc.).
4. Optionally select a **Texture Directory** to override texture lookup.
5. Choose a **Resolution**.
6. Click **Launch Viewer**.

The renderer's output streams into the console pane in real time. Use **Stop** to terminate the renderer or **Clear** to reset the console.

### Command-line arguments passed to the renderer

| Flag | Description |
|------|-------------|
| `--filename` | Path to the model file |
| `--texture` | Optional texture or manifest file |
| `--resource_path` | Optional texture lookup directory |
| `-r` | Resolution (e.g. `1920x1080`) |
| `-p` | Application directory path |

### Settings

Go to **Help → Settings** to configure a custom path to the renderer executable if auto-detection or your system `PATH` is not sufficient.

## Included Data

The `data/` directory ships with a collection of sample models and textures:

- Various `.mxmod` models (UFOs, ships, trees, planets, geometric shapes, etc.)
- Texture files in `.tex` and `.png` formats
- Pre-compiled SPIR-V shaders (`.spv`) and GLSL sources for the Vulkan backend
- A TrueType font for in-viewer text rendering

## WebAssembly Build

A web-based build is available under the `web/` directory for running the viewer in a browser via Emscripten.

## License

Part of the [libmx2](https://github.com/lostjared/libmx2) project. See the repository root for license details.
