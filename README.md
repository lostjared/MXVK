# MXVK

<img width="640" height="214" alt="mxvk_logo_640" src="https://github.com/user-attachments/assets/88c614ba-c7d1-4001-bcba-313b9da5450c" />

MXVK is a C++20 Vulkan rendering framework with SDL3 integration, focused on practical 2D and 3D application development.

It provides a reusable window/render loop (`mxvk::VK_Window`), sprite and text rendering, model rendering, optional OpenCV capture support, and a set of examples that demonstrate end-to-end usage. It is designed to be easy to use while still retaining the power that Vulkan provides.

## What This Project Is

- A static library (`mxvk`) for Vulkan-based rendering.
- A set of examples under `examples/` that exercise key features:
	- dynamic rendering and custom pipelines
	- sprite rendering and shader effects
	- text rendering
	- Matrix-style digital rain rendering
	- model rendering
	- game loops and input handling
	- simple gameplay examples and UI state flow
	- optional OpenCV camera/video workflows

## Demo Video

View on YouTube: https://youtu.be/Y3PyGg3qBUA

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

If you clone/move the repo to a different machine or path, run a fresh configure first.
CMake-generated build files contain absolute paths and can fail with errors like
`No rule to make target .../examples/.../*.frag` when a previous build directory is reused.

```bash
cmake --fresh -S . -B build
cmake --build build -j
```

If your CMake version does not support `--fresh`, remove the build directory manually:

```bash
rm -rf build
cmake -S . -B build
cmake --build build -j
```

Useful CMake options:

- `-DVALIDATION=ON` enables Vulkan validation layers.
- `-DDEBUG_MODE=ON` enables debug compile flags.
- `-DWITH_CUDA=AUTO|ON|OFF` controls CUDA acceleration/interop. The default is `AUTO`, which enables CUDA when the toolkit is detected; `ON` requires CUDA; `OFF` disables it.
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

To run a compiled example from the repository root, use `run.pl`:

```bash
./run.pl <example> [extra args...]
```

`run.pl` looks up the built executable under `build/examples/<example>/` and
passes the example's asset directory with `-p` automatically.

Examples:
```bash
./run.pl sprite_example -r 1920x1080 -f
./run.pl model_example
./run.pl planet
./run.pl pong
./run.pl tictactoe
./run.pl pool_demo
./run.pl fractal_zoom
./run.pl console_demo -r 1280x720
./run.pl matrix
./run.pl glitch_cube -r 1280x720
./run.pl starship
./run.pl walk
./run.pl compute_shader --camera 0
./run.pl opencv_example --camera 0 -r 1280x720
./run.pl opencv_model --filename ./models/torus.mxmod.z --camera 0
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

### `matrix`
- Matrix-style digital rain demo built with `SDL_ttf` glyph rendering and a sprite-backed framebuffer.
- Uses the bundled `data/keifont.ttf` runtime asset and `Space` to randomize the streams.

### `model_example`
- Demonstrates `VKAbstractModel` loading and rendering of 3D assets.
- Uses uniform buffers and camera/projection transforms.

### `planet`
- Planetary ring and satellite rendering demo built on `VKAbstractModel`.
- Shows textured 3D model rendering with lighting, shader-driven materials, and runtime asset staging.

### `tux_example`
- Renders a 3D model with a textured animated background and on-screen text.

### `starship`
- 3D ship viewer with textured Phong shading, a moving starfield, and a small exhaust effect.
- Shows how to layer model rendering with sprite-based background effects.

### `glitch_cube`
- MXVK Vulkan port of the legacy `gl_glitch_cube` demo from MX2.
- Uses `VKAbstractModel` with GLSL 450 shaders (`model.vert/.frag`) and runtime-compiled SPIR-V.
- Controls:
	- Drag left mouse to orbit the cube.
	- Mouse wheel zooms the camera in and out.
	- `Space` toggles rotation axis behavior.
	- `PageUp`/`PageDown` scales the cube larger/smaller at runtime.

### `asteroids`
- Full game example with game state management, controller/keyboard input, particles, and HUD.
- Good reference for a complete gameplay loop on top of MXVK.

### `pong`
- 3D-styled Pong demo with paddle/ball gameplay implemented on top of MXVK rendering hooks.
- Demonstrates runtime asset copying, model-based scene elements, and real-time game-state updates.

### `tictactoe`
- Mouse-driven tic-tac-toe example with a simple computer opponent.
- Shows sprite-based board rendering, text UI, click-to-play interaction, and quick restart flow with `R` or after a finished game.
- Uses the shared font asset plus a background image staged into the example output directory.

### `walk`
- First-person maze and exploration sample with procedural level generation.
- Demonstrates collision handling, projectile combat, collectibles, and a debug console in an FPS-style loop.

### `pool_demo` (`Pool3D` executable)
- 3D pool/billiards demo built directly on MXVK dynamic rendering.
- Demonstrates multi-model scene composition, per-object transforms, and interactive cue/ball simulation flow.

### `puzzle`
- Puzzle game example ported to MXVK/SDL3.
- Demonstrates menu/game/scores state flow, sprite-based gameplay grid, and text UI.

### `tetris`
- 3D Tetris game in MXVK
- Puzzle game

### `fractal_zoom`
- Fullscreen fractal renderer with its own shader pipeline.
- Demonstrates per-example shader compilation and runtime shader-path wiring.

### `console_demo`
- Immediate-mode style in-app console and command handling demo.
- Demonstrates custom post-build asset staging (font/texture/shaders) and runtime data loading.

### `compute_shader` (requires `-DCV=ON`)
- Streams camera frames through selectable Vulkan compute shaders and displays the processed result.
- Useful for testing OpenCV capture, GPU image processing, and shader hot selection from `data/index.txt`.

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

## Early Screenshots

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/18c9ead9-c935-4847-92f7-d6560577f8e6" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/3560a610-992a-4c8f-b6bc-379473b6a171" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/28dc95f8-d24b-4d70-ba95-b71eb50b8ebf" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/8ffc652d-e3bf-4cab-821c-0e58de6f46d2" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/41f583b6-ecda-4c13-8d90-82290e9512a9" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/f7daf971-5588-41b5-ae7b-b3768a2ce611" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/045f4929-337e-4e28-a828-2a2d27be1665" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/bde0541a-3c5e-47f3-9ab0-e62757d524a9" />

- If you run an example from a custom working directory, prefer passing `-p` with the correct asset root.
