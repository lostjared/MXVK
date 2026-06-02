# MXVK

MXVK is a C++20 Vulkan rendering framework with SDL3 integration, focused on practical 2D and 3D application development.
It provides a reusable window/render loop (`mxvk::VK_Window`), sprite and text rendering, model rendering, optional OpenCV capture support, and a set of examples that demonstrate end-to-end usage.

## What This Project Is

- A static library (`mxvk`) for Vulkan-based rendering.
- A set of examples under `examples/` that exercise key features:
	- dynamic rendering and custom pipelines
	- sprite rendering and shader effects
	- text rendering
	- model rendering
	- game loops and input handling
	- optional OpenCV camera/video workflows

## Core Dependencies

The root CMake configuration checks for and uses:

- C++20 compiler
- SDL3
- SDL3_ttf
- Vulkan 1.4+
- PNG
- ZLIB
- glm
- glslc (shader compiler)
- Optional: OpenCV (when building with `-DCV=ON`)

## Build

From repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

Useful CMake options:

- `-DVALIDATION=ON` enables Vulkan validation layers.
- `-DDEBUG_MODE=ON` enables debug compile flags.
- `-DCV=ON` enables OpenCV-based examples and capture support.
- `-DMIXER=ON` enables SDL3_mixer audio support (`mxvk_sound.cpp`, `MXVK_WITH_MIXER`).
- `-DJPEG=ON` enables JPEG image support (`mxvk_jpeg.cpp`, `MXVK_WITH_JPEG`).
- `-DEXAMPLES=OFF` builds/install only the `mxvk` library and skips all examples.

Example:

```bash
cmake -S . -B build -DVALIDATION=ON -DCV=ON
cmake --build build -j
```

Additional configure examples:

```bash
# Build library + examples with audio and JPEG support
cmake -S . -B build -DMIXER=ON -DJPEG=ON

# Library-only build (faster CI/package build)
cmake -S . -B build -DEXAMPLES=OFF
```

## Command Line Arguments

Most examples use the shared parser in `mxvk/argz.hpp` via `proc_args(...)`.

Supported options:

- `-h`
	- Show help and exit.
- `-p <path>`, `--path <path>`
	- Asset root path (defaults to `.` when omitted).
- `-r <WxH>`, `--resolution <WxH>`
	- Window resolution, e.g. `-r 1280x720`.
- `-f`, `--fullscreen`
	- Launch in fullscreen mode.
- `--filename <file>`
	- Optional input/model/video filename (used by specific examples).
- `--texture <file>`
	- Optional texture filename.
- `-S <path>`, `--shader-path <path>`
	- Shader SPIR-V folder path.
- `--camera <index>`
	- Camera index for OpenCV capture examples.

General run pattern:

```bash
./<example> [options]
```

Examples (in each subproject build directory):
```bash
./sprite_example -r 1920x1080 -f
./model_example -p ./examples/model_example
./pong -p ./examples/pong
./Pool3D -p ./examples/pool_demo
./fractal_zoom
./console_demo -r 1280x720
./opencv_example --camera 0 -r 1280x720
./opencv_model --filename ./models/torus.mxmod.z --camera 0
```

## Examples

Current example executables:

### `hello_world`
- Demonstrates a minimal custom Vulkan pipeline in an `mxvk::VK_Window` subclass.
- Shows how to handle swapchain recreation hooks and render loop integration.

### `static_example`
- Similar to `hello_world`, but focused on a static shader-driven fullscreen render path.
- Useful for understanding custom rendering hooks and command recording.

### `sprite_example`
- Demonstrates sprite loading from PNG and full-window sprite rendering.
- Includes text drawing and custom sprite shader usage.

### `text_example`
- Minimal example for text rendering with `setFont(...)` and `printText(...)`.

### `model_example`
- Demonstrates `VKAbstractModel` loading and rendering of 3D assets.
- Uses uniform buffers and camera/projection transforms.

### `tux_example`
- Renders a 3D model with a textured animated background and on-screen text.

### `asteroids`
- Full game example with game state management, controller/keyboard input, particles, and HUD.
- Good reference for a complete gameplay loop on top of MXVK.

### `pong`
- 3D-styled Pong demo with paddle/ball gameplay implemented on top of MXVK rendering hooks.
- Demonstrates runtime asset copying, model-based scene elements, and real-time game-state updates.

### `pool_demo` (`Pool3D` executable)
- 3D pool/billiards demo built directly on MXVK dynamic rendering.
- Demonstrates multi-model scene composition, per-object transforms, and interactive cue/ball simulation flow.

### `puzzle`
- Puzzle game example ported to MXVK/SDL3.
- Demonstrates menu/game/scores state flow, sprite-based gameplay grid, and text UI.

### `fractal_zoom`
- Fullscreen fractal renderer with its own shader pipeline.
- Demonstrates per-example shader compilation and runtime shader-path wiring.

### `console_demo`
- Immediate-mode style in-app console and command handling demo.
- Demonstrates custom post-build asset staging (font/texture/shaders) and runtime data loading.

### `opencv_example` (requires `-DCV=ON`)
- Displays camera or video-file frames on a sprite in real time.
- Uses `--camera` and/or `--filename` to select source.

### `opencv_model` (requires `-DCV=ON`)
- Streams camera frames into a model texture.
- Demonstrates live texture updates on 3D geometry with model rendering.

## Project Layout

- `mxvk/`
	- Engine source, headers, and built-in shaders.
- `examples/`
	- Runnable examples and sample assets.
- `models/`
	- Model assets used by model-based examples.
- `volk/`
	- Vulkan function loader submodule/source.

## Notes

- The example CMake files copy required runtime assets into each example's output directory after build.
- 
## Early Screenshots

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/18c9ead9-c935-4847-92f7-d6560577f8e6" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/3560a610-992a-4c8f-b6bc-379473b6a171" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/28dc95f8-d24b-4d70-ba95-b71eb50b8ebf" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/8ffc652d-e3bf-4cab-821c-0e58de6f46d2" />

- If you run an example from a custom working directory, prefer passing `-p` with the correct asset root.
