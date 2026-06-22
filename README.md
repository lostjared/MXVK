# MXVK

<img width="640" height="214" alt="mxvk_logo_640" src="https://github.com/user-attachments/assets/88c614ba-c7d1-4001-bcba-313b9da5450c" />

MXVK is a C++20 Vulkan rendering framework with SDL3 integration, focused on practical 2D and 3D application development.

It provides a reusable window/render loop (`mxvk::VK_Window`), sprite and text rendering, model rendering, optional OpenCV capture support, and a set of examples that demonstrate end-to-end usage. It is designed to be easy to use while still retaining the power that Vulkan provides.

The repository also includes MXWrite, a small FFmpeg-based video writer library for exporting RGBA frames to video files. It can be built alongside MXVK with `-DWITH_MXWRITE=AUTO|ON|OFF`.

## Contents

- [What This Project Is](#what-this-project-is)
- [Core Dependencies](#core-dependencies)
- [Build](#build)
- [Command Line Arguments](#command-line-arguments)
- [Examples](#examples)
- [MXWrite](#mxwrite)
- [Project Layout](#project-layout)
- [Early Screenshots](#early-screenshots)


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
- Optional: FFmpeg for MXWrite (when building with `-DWITH_MXWRITE=AUTO|ON`)


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
- `-DWITH_MXWRITE=AUTO|ON|OFF` controls the MXWrite FFmpeg video writer build. The default is `AUTO`, which builds MXWrite when FFmpeg is detected; `ON` requires FFmpeg; `OFF` skips MXWrite.
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
	- For `compute_shader`, this also sets the compute canvas and recorded video size; source frames are scaled into that canvas before shader processing. When omitted for file playback, the window and compute canvas use the video frame size unless fullscreen is enabled.
- `-f`, `--fullscreen`
	- Launch in fullscreen mode.
- `--filename <file>`
	- Optional input/model/video filename (used by specific examples).
- `-o <file>`, `--output <file>`
	- Optional output filename for examples that export video.
- `-c <value>`, `--crf <value>`
	- Optional Constant Rate Factor for video export.
- `--encode-preset <preset>`
	- Optional MXWrite encoder preset. For software x264 use values such as `ultrafast`, `superfast`, `veryfast`, `fast`, `medium`, `slow`, or `veryslow`; for NVENC these map to NVENC preset levels.
- `--encode-tune <tune>`
	- Optional MXWrite encoder tune, such as `film`, `animation`, `grain`, `stillimage`, `fastdecode`, or `zerolatency`.
- `--encode-codec <auto|software|nvenc>`
	- Optional MXWrite encoder backend policy. `auto` prefers NVENC when available, `software` forces x264, and `nvenc` requests NVENC with fallback handled by MXWrite.
- `--encode-realtime`
	- Enable MXWrite low-latency/realtime encoder settings.
- `--mxwrite-block`
	- Make MXWrite block when its internal queue is full instead of dropping frames.
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

For the full core demo sweep, use `./run.pl --all`. It runs the example list
sequentially by delegating to `testapps.pl`, forwards any extra arguments to
each `run.pl` invocation, and stops on the first failure or `Ctrl-C`.

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

The examples are grouped below by what they demonstrate. Many of them share the same `run.pl` invocation pattern and accept the common arguments documented above.

### Starter samples

- `cfg_example` - small configuration API smoke test that reads and updates `test.dat`.
- `hello_world` - minimal `mxvk::VK_Window` example with a custom graphics pipeline and animated triangle.
- `static_example` - fullscreen triangle sample that pushes window size and frame count into the shader.
- `sprite_example` - loads a PNG sprite, renders it full-screen, and overlays text with a custom sprite shader.
- `text_example` - compact text-rendering sample built around `setFont(...)` and `printText(...)`.

### Shader and effect demos

- `matrix` - Matrix-style digital rain rendered from `SDL_ttf` glyphs and a sprite-backed framebuffer.
- `binary_matrix` - 3D variant of `matrix` that turns `0` and `1` glyphs into a depth-aware scene.
- `fractal_zoom` - fullscreen Mandelbrot-style renderer with runtime zoom, pan, palette switching, and shader-driven color output.
- `console_demo` - in-window console layered over a moving shader background.
- `glitch_cube` - stylized cube viewer with time-based transforms, shader-driven presentation, and runtime scale/orbit controls.

### 3D viewers

- `model_example` - basic `VKAbstractModel` viewer for textured OBJ or MXMOD assets.
- `planet` - textured Saturn scene with a ring, orbital camera, and runtime asset staging.
- `tux_example` - layered scene that combines a textured model, an animated background sprite, and text overlays.
- `sprite3d_example` - 3D sprite scene with a starfield, a flying saucer, and mouse-driven camera orbiting.
- `starship` - ship viewer with Phong shading, a moving starfield, and exhaust effects.
- `dark` - Dark Crystal Pyramid viewer with a custom beam effect and layered model/sprite/text rendering.
- `moon` - moon model viewer with pyramids and a separate starfield layer.

### Games and interactive scenes

- `asteroids` - 2D Asteroids-style arcade shooter with physics, scoring, particles, and a fixed playfield.
- `asteroids3d` - 3D Asteroids-style action game with ship, asteroids, projectiles, and console commands.
- `pong` - 3D-styled Pong demo with paddle/ball gameplay and real-time state updates.
- `tictactoe` - mouse-driven tic-tac-toe against a simple computer opponent.
- `walk` - first-person maze and exploration sample with procedural generation, collision, collectibles, and combat.
- `pool_demo` (`Pool3D`) - 3D billiards game with menus, high scores, cue-ball placement, and shot logic.
- `puzzle` - Acid Drop, a falling-block puzzle with menus, scores, options, credits, and name entry.
- `masterpiece` (`MasterPiece`) - port of the original `MasterPiece.SDL` block puzzle game with updated assets.
- `tetris` - 3D Tetris with a title flow, high scores, credits, and optional network multiplayer.

### Camera and video workflows

- `compute_shader` - OpenCV capture or video-file playback through selectable Vulkan compute shaders. Requires `-DCV=ON`.
- `opencv_example` - displays camera or video frames on a sprite in real time. Requires `-DCV=ON`.
- `opencv_model` - maps a live camera feed onto a textured 3D model. Requires `-DCV=ON`.

If you want a quick tour of the core demos, `./run.pl --all` executes the default example sweep used by `testapps.pl`.


## MXWrite

MXWrite is the FFmpeg-based video writer library included in this repository. It is useful when you want to export RGBA frames to a video file from a C++20 application without building a separate project.

### Build

MXWrite is included from the root CMake build and follows the same auto-detect pattern as other optional components:

```bash
cmake -S . -B build -DWITH_MXWRITE=AUTO
cmake --build build -j
```

Use `-DWITH_MXWRITE=ON` to require FFmpeg, or `-DWITH_MXWRITE=OFF` to skip MXWrite entirely.

### Usage

Consumer code includes [`MXWrite/mxwrite.hpp`](MXWrite/mxwrite.hpp) and links against `mxwrite`. The public API provides `Writer` for frame-based or timestamp-based output, plus `EncodeOptions` for preset, codec, CRF, realtime, and HDR settings.

```cpp
#include "mxwrite.hpp"

Writer writer;
EncodeOptions opts;
opts.codec = "auto";
opts.crf = 18;

if (writer.open("output.mp4", 1280, 720, 30.0f, opts)) {
    writer.write(rgba_frame_ptr);
    writer.close();
}
```

When built through MXVK, the library exports `MXWRITE_ENABLED=1` on the `mxwrite` target so consumers can gate MXWrite-specific code consistently.

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

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/b7d0af91-993a-44ab-8045-126459df13a1" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/3560a610-992a-4c8f-b6bc-379473b6a171" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/28dc95f8-d24b-4d70-ba95-b71eb50b8ebf" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/8ffc652d-e3bf-4cab-821c-0e58de6f46d2" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/41f583b6-ecda-4c13-8d90-82290e9512a9" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/f7daf971-5588-41b5-ae7b-b3768a2ce611" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/045f4929-337e-4e28-a828-2a2d27be1665" />

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/bde0541a-3c5e-47f3-9ab0-e62757d524a9" />

- If you run an example from a custom working directory, prefer passing `-p` with the correct asset root.
