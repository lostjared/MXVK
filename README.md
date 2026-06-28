# MXVK

<img width="640" height="214" alt="mxvk_logo_640" src="https://github.com/user-attachments/assets/88c614ba-c7d1-4001-bcba-313b9da5450c" />

MXVK is a C++20 Vulkan rendering framework with SDL3 integration, focused on practical 2D and 3D application development.

It provides a reusable window/render loop (`mxvk::VK_Window`), sprite and text rendering, model rendering, a small engine math library in `mxvk/mxvk_math.h`, optional OpenCV capture support, and a set of examples that demonstrate end-to-end usage. It is designed to be easy to use while still retaining the power that Vulkan provides.

The repository also includes MXWrite, a small FFmpeg-based video writer library for exporting RGBA frames to video files. It can be built alongside MXVK with `-DWITH_MXWRITE=AUTO|ON|OFF`.

## Contents

- [What This Project Is](#what-this-project-is)
- [Core Dependencies](#core-dependencies)
- [Build](#build)
- [Command Line Arguments](#command-line-arguments)
- [VK_Window Post-Processing](#vk-window-post-processing)
- [Debugging Examples](#debugging-examples)
- [Examples](#examples)
- [Recent Optimizations](#recent-optimizations)
- [MXWrite](#mxwrite)
- [MXNetwork](#mxnetwork)
- [Project Layout](#project-layout)
- [Early Screenshots](#early-screenshots)


<a id="what-this-project-is"></a>

## What This Project Is

- A static library (`mxvk`) for Vulkan-based rendering.
- A small 2D/3D math and software-raster helper library used by the engine tests and examples.
- A set of examples under `examples/` that exercise key features:
	- dynamic rendering and custom pipelines
	- sprite rendering and shader effects
	- text rendering
	- engine math, projection, and software 3D raster tests
	- Matrix-style digital rain rendering
	- reusable Matrix rain texture generation
	- model rendering
	- game loops and input handling
	- controller input, console overlays, post-processing, and audio-enabled gameplay when optional features are present
	- simple gameplay examples and UI state flow
	- optional OpenCV camera/video workflows

## Demo Video

View on YouTube: https://youtu.be/Y3PyGg3qBUA


<a id="core-dependencies"></a>

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


<a id="build"></a>

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
- `-DFRACTAL_ZOOM=ON` enables the `fractal_zoom` example and its Boost dependency.
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

# Build the fractal_zoom example and its Boost dependency
cmake -S . -B build -DFRACTAL_ZOOM=ON

# Library-only build (faster CI/package build)
cmake -S . -B build -DEXAMPLES=OFF
```


<a id="command-line-arguments"></a>

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
	- Optional input/model/video filename used by examples that support external assets.
	- For `model_example`, this overrides the default `data/pyramid.obj` mesh. You can point it at a custom `.obj`, `.mxmod`, or `.mxmod.z` file.
	- When you provide a custom model, the loader no longer assumes the built-in pyramid asset set. If the model references external textures, use `--resource <file>` for a texture manifest and `--resource_path <dir>` for the texture directory.
	- `run.pl` already passes the example asset root with `-p`, so `--filename` can usually be a relative path inside the repo or a full absolute path.
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
./run.pl 3dmath
./run.pl 3dmath_cube
./run.pl 3dmath_texture --filename ./examples/sprite_example/data/intro.png
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
./run.pl defender
./run.pl walk
./run.pl 3dmath_masterpiece
./run.pl compute_shader --camera 0
./run.pl opencv_example --camera 0 -r 1280x720
./run.pl opencv_model --filename ./models/torus.mxmod.z --camera 0
```

<a id="vk-window-post-processing"></a>

## VK_Window Post-Processing

`mxvk::VK_Window` can render the normal scene into an offscreen color target and then draw that image through an optional full-screen fragment shader before presenting. Attach a sprite-compatible fragment shader with `attachPostProcessingShader(...)`, pass effect parameters with `setPostProcessingShaderParams(...)`, enable automatic elapsed-time updates for parameter 1 with `setPostProcessingShaderTimeEnabled(true)`, and toggle the pass at runtime with `setPostProcessingEnabled(...)`.

`defender` and `asteroids3d` use this path for the CRT shader. In both examples, press `F8` to toggle the CRT post-processing effect on or off.

<a id="debugging-examples"></a>

## Debugging Examples

Use `debug.pl` from the repository root to launch a built example under GDB:

```bash
./debug.pl <example> [extra args...]
```

The script resolves the executable name from `examples/<example>/CMakeLists.txt`, then looks for it under `build/examples/<example>/`. It changes into the executable directory before launching GDB so relative runtime files behave like a normal `run.pl` launch.

`debug.pl` also passes the example asset path automatically with `-p`. Most examples receive `-p examples/<example>`, while examples whose CMake file sets `ASSET_DIR` to the target output directory receive `-p build/examples/<example>`. Any extra arguments after the example name are forwarded to the program.

Internally, the script executes GDB in quiet mode and immediately starts the program:

```bash
gdb -q -ex "set confirm off" -ex "run" --args ./<executable> -p <asset_path> [extra args...]
```

When the program crashes or hits a breakpoint, you are left at the GDB prompt. Useful commands include `bt` for a backtrace, `frame <n>` to inspect a stack frame, `print <expr>` to inspect values, `continue` to resume, and `quit` to exit.

Examples:

```bash
./debug.pl sprite_example -r 1920x1080
./debug.pl model_example --filename ./models/torus.mxmod.z
./debug.pl opencv_example --camera 0 -r 1280x720
```

<a id="examples"></a>

## Examples

The examples are grouped below by what they demonstrate. Most accept the shared arguments documented above, and the per-example `README.md` files carry the full control maps for the larger demos.

### Starter Samples

- `cfg_example` - config persistence smoke test. **Inputs:** none. **Controls:** none; it runs once, prints the incremented counter, and exits.
- `hello_world` - minimal `mxvk::VK_Window` example with a custom graphics pipeline and animated triangle. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `skeleton` - smallest practical subclass of `mxvk::VK_Window`, intended as a copyable starting point for new examples. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `static_example` - fullscreen triangle sample that pushes window size and frame count into the shader. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `sprite_example` - loads a PNG sprite, renders it full-screen, and overlays text with a custom sprite shader. **Inputs:** common `-p`, `-r`, `-f`; optional texture and shader path arguments. **Controls:** `Escape` quits.
- `text_example` - compact text-rendering sample built around `setFont(...)` and `printText(...)`. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `rain` - static helper library used by Matrix-style examples to render configurable glyph rain into an SDL surface and MXVK sprite texture. It is not launched directly through `run.pl`.

### Engine Math Tests

These programs are not intended as standalone applications. They are small visual tests for the engine's `mxvk/mxvk_math.h` math helpers, projection code, triangle drawing, filled polygons, lighting, and software texture sampling inside the MXVK render loop.

- `3dmath` - minimal rotating triangle test using `vec4D`, `Mat4D`, `RenderList`, and `PipeLine` projection/drawing helpers. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `3dmath_cube` - rotating cube test for matrix transforms, backface culling, depth sorting, diffuse face shading, filled triangle rasterization, and line clipping. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `3dmath_texture` - textured rotating cube test for the same software 3D path plus PNG loading and software UV sampling. **Inputs:** common `-p`, `-r`, `-f`, plus `--filename <file.png>` or `--texture <file.png>`. **Controls:** `Escape` quits.
- `3dmath_masterpiece` - MasterPiece variant that renders the board and falling blocks as CPU-rasterized spinning 3D cubes before uploading the frame through an MXVK sprite. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** arrow keys move, `Up` or `A` rotates forward, `S` rotates backward, `P` pauses, `Escape` returns to the menu.

### Shader And Effect Demos

- `matrix` - Matrix-style digital rain rendered from `SDL_ttf` glyphs and a sprite-backed framebuffer. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Space` randomizes the streams, `Escape` quits.
- `binary_matrix` - 3D variant of `matrix` that turns `0` and `1` glyphs into a depth-aware scene. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Space` randomizes the rain, arrow keys orbit the camera, `Page Up` / `Page Down` zoom, `Escape` quits.
- `fractal_zoom` - fullscreen Mandelbrot-style renderer with runtime zoom, pan, palette switching, and shader-driven color output. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** mouse wheel zoom, drag pan, `W` / `A` / `S` / `D` or arrow keys pan, `Z` / `X` continuous zoom, `1` / `2` / `3` presets, `+` / `=` and `-` iteration count, `[` / `]` palette, `R` reset, `Escape` quit.
- `console_demo` - in-window console layered over a moving shader background. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `F3` opens or closes the console, `Escape` quits when the console is hidden, console commands include `help`, `echo`, `about`, `quit`, and `exit`.
- `glitch_cube` - stylized cube viewer with time-based transforms, shader-driven presentation, and runtime scale/orbit controls. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** left mouse drag orbits, mouse wheel zooms, `Space` toggles the rotation axis, `Page Up` / `Page Down` scales the cube, `Escape` quits.

### 3D Viewers

- `model_example` - basic `VKAbstractModel` viewer for textured OBJ or MXMOD assets. By default it loads `data/pyramid.obj` from the example asset directory. `--filename` overrides the model file, `--resource` can point at a texture manifest, `--resource_path` can override the texture lookup directory, and `--binary` replaces the model's texture with animated Matrix-style green rain while also enabling the skybox toggle. **Inputs:** common `-p`, `-r`, `-f`, `--filename`, `--fragment`, `--resource`, `--resource_path`, and `--binary`. **Controls:** left mouse drag rotates, mouse wheel zooms, `Space` toggles auto-spin, `Enter` toggles skybox mode when `--binary` is enabled, `W/A/S/D` look around inside the skybox view, `Escape` quits.
- `planet` - textured Saturn scene with a ring, orbital camera, and runtime asset staging. **Inputs:** common `-p`, `-r`, `-f`, `--filename`. **Controls:** left mouse drag orbits, mouse wheel zooms, `Escape` quits.
- `tux_example` - layered scene that combines a textured model, an animated background sprite, and text overlays. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Escape` quits.
- `sprite3d_example` - 3D sprite scene with a starfield, a flying saucer, and mouse-driven camera orbiting. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** left mouse drag orbits, mouse wheel zooms, `Escape` quits.
- `starship` - ship viewer with Phong shading, a moving starfield, and exhaust effects. **Inputs:** common `-p`, `-r`, `-f`, plus the example asset set. **Controls:** mouse drag rotates the ship, `Escape` quits.
- `dark` - Dark Crystal Pyramid viewer with a custom beam effect and layered model/sprite/text rendering. **Inputs:** common `-p`, `-r`, `-f`, optional `--filename`. **Controls:** left mouse drag orbits, mouse wheel zooms, `Escape` quits.
- `moon` - moon model viewer with pyramids and a separate starfield layer. **Inputs:** common `-p`, `-r`, `-f`, optional `--filename` and `--fragment`. **Controls:** left mouse drag orbits, mouse wheel zooms, `Space` toggles auto-spin, `Escape` quits.

### Games And Interactive Scenes

- `asteroids` - 2D Asteroids-style arcade shooter with physics, scoring, particles, and a fixed playfield. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Left` / `Right` rotate, `Up` thrust, `Space` fire, `Escape` quits.
- `asteroids3d` - 3D Asteroids-style action game with ship, asteroids, projectiles, and console commands. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Space` or `Enter` starts from the intro, `F1` toggles the debug HUD, `F2` toggles inverted controls, `F3` opens the console, `F8` toggles CRT post-processing, `Left` / `Right` yaw, `W` / `S` pitch, `A` / `D` roll, `Up` / `Down` speed, `Space` fires, `Escape` returns to the intro or quits.
- `defender` - side-scrolling 3D starfield shooter with model-based ship and asteroids, animated UFO sprites, radar/HUD, controller support, console commands, Matrix-rain intro, CRT post-processing, and optional SDL3_mixer audio. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Enter` or `Space` starts, `Z` thrusts, `D` boosts, `X` reverses, `W` / `Up` and `Down` move vertically, `A` / `S` roll, `Space` fires, `F3` toggles the console, `F4` toggles FPS, `F8` toggles CRT, `Escape` quits.
- `pong` - 3D-styled Pong demo with paddle/ball gameplay and real-time state updates. **Inputs:** common `-p`, `-r`, `-f` plus the example data directory. **Controls:** arrow keys move the paddle, `W` / `A` / `S` / `D` rotate the view, `Q` resets rotation, `R` resets the game, `Space` toggles wireframe, `Enter` resets the camera, `Escape` quits.
- `tictactoe` - mouse-driven tic-tac-toe against a simple computer opponent. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** mouse click places a mark, `R` resets, `Escape` quits.
- `walk` - first-person maze and exploration sample with procedural generation, collision, collectibles, and combat. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `W` / `A` / `S` / `D` move, mouse or right stick look, `Left Shift` sprint, `Left Ctrl` crouch, `Space` jump, left click or right shoulder fire, `F` toggles the FPS overlay, `Escape` releases the mouse or quits.
- `pool_demo` (`Pool3D`) - 3D billiards game with menus, high scores, cue-ball placement, and shot logic. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** menus use `Enter`, `Space`, `Escape`, and `Back`; in-game controls include arrow keys, `Space` to charge a shot, `Enter` to confirm cue-ball placement, mouse drag for aiming, right mouse drag to rotate the camera, and wheel or pinch to zoom.
- `puzzle` - Acid Drop, a falling-block puzzle with menus, scores, options, credits, and name entry. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** `Up` / `Down` navigate menus, `Left` / `Right` move blocks, `Space` rotates, `P` pauses, `Escape` backs out or quits, and text entry uses `Backspace` / `Enter`.
- `masterpiece` (`MasterPiece`) - port of the original `MasterPiece.SDL` block puzzle game with updated assets. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** menu navigation uses `Up` / `Down` / `Enter` / `Escape`; in game use `Left` / `Right` move, `Down` soft drop, `A` or `Up` rotate forward, `S` rotate backward, `P` pause, `Escape` return to menu, with typed input for high-score entry.
- `tetris` - 3D Tetris with a title flow, high scores, credits, and optional network multiplayer. **Inputs:** common `-p`, `-r`, `-f`. **Controls:** menus use `Up` / `Down` / `Enter` / `Escape`; in game use `Left` / `Right` move, `Down` soft drop, `Up` rotate, `Z` hard drop, `R` restart, `Escape` menu, camera controls use `W` / `A` / `S` / `D`, `Q` / `E`, `Page Up` / `Page Down`, and multiplayer uses `H` to host and `J` / `Enter` to join.

### Camera And Video Workflows

- `compute_shader` - OpenCV capture or video-file playback through selectable Vulkan compute shaders. **Inputs:** requires `-DCV=ON`; accepts `--camera`, `--filename`, `--output`, `--crf`, `--shader-path`, and compute-related options. **Controls:** `Up` / `Down` switch shaders, `Left` / `Right` change the `acidcam_filters` selector, `Escape` quits.
- `opencv_example` - displays camera or video frames on a sprite in real time. **Inputs:** requires `-DCV=ON`; accepts `--camera` and `--filename`. **Controls:** `Escape` quits.
- `opencv_model` - maps a live camera feed onto a textured 3D model. **Inputs:** requires `-DCV=ON`; accepts `--camera`, `--filename`, and standard example arguments. **Controls:** left mouse drag orbits, mouse wheel zooms, arrow keys rotate the model, `Escape` quits.

If you want a quick tour of the core demos, `./run.pl --all` executes the default example sweep used by `testapps.pl`.

<a id="recent-optimizations"></a>

## Recent Optimizations

- Sprite and 3D sprite paths now keep more GPU state alive across frames, track dirty state, and support instanced sprite batches. This reduces repeated descriptor/pipeline work in sprite-heavy demos such as `binary_matrix`, `defender`, `starship`, and `pong`.
- Text rendering caches uploaded glyph/text textures and evicts old entries instead of recreating Vulkan images for repeated strings every frame. HUD-heavy examples use this path for steadier frame times.
- `compute_shader` now presents the Vulkan compute output through a full-screen sampled draw, avoiding a readback or sprite upload for display. When CUDA interop is available, GPU capture frames can be copied directly into the Vulkan compute input image; otherwise the CPU fallback path remains available.
- `fractal_zoom` uses cached adaptive reference-orbit data, throttled rebuilds while the view is changing, and a direct shader path while coordinates are still safe for f32. Deep zooms switch to perturbation data only when needed.
- Matrix rain is factored into the reusable `rain` library so `matrix`, `binary_matrix`, `model_example`, `planet`, and `defender` can share glyph loading, tinting, resize handling, and texture sync code.


<a id="mxwrite"></a>

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

<a id="mxnetwork"></a>

## MXNetwork

MXNetwork is the small C++20 socket library included in this repository. It provides a C-compatible low-level socket API plus a move-only C++ RAII wrapper for IPv4/IPv6 TCP and UDP, plus Unix-domain socket workflows.

### Build

MXNetwork is included from the root CMake build as the static `mxnetwork` library. The root build disables MXNetwork's standalone examples and uses the library where needed, such as the optional multiplayer path in `tetris`.

```bash
cmake -S . -B build
cmake --build build -j
```

To build MXNetwork directly with its own examples, configure the `MXNetwork` subdirectory:

```bash
cmake -S MXNetwork -B build-mxnetwork
cmake --build build-mxnetwork -j
```

MXNetwork's standalone build includes TCP, UDP, Unix-domain socket, relay, and file-download examples. TCP and UDP examples use the same wrapper methods that are available to IPv4 and IPv6 socket types. Use `-DMXNETWORK_EXAMPLES=OFF` when building that subdirectory if you only want the library.

### Usage

Consumer code includes [`MXNetwork/include/mxnetwork/socket.hpp`](MXNetwork/include/mxnetwork/socket.hpp) and links against `libmxnetwork::mxnetwork` when using this repository directly. Installed consumers can use `find_package(mxnetwork REQUIRED)` and link against `mxnetwork::mxnetwork`.

The C++ API provides `mxnetwork::Socket` for move-only socket ownership, plus `mxnetwork::MXNetworkInit` for platform socket initialization and cleanup.

```cpp
#include "mxnetwork/socket.hpp"

#include <iostream>
#include <string>

int main() {
    mxnetwork::MXNetworkInit network_init;
    mx_socket_ignore_pipe_signal();

    mxnetwork::Socket socket(mxnetwork::SocketType::TYPE_INET);
    if (!socket.connect("127.0.0.1", "8080")) {
        std::cerr << "connect failed\n";
        return 1;
    }

    const std::string message = "hello";
    socket.write_all(message.data(), message.size());
}
```

The wrapper supports IPv4 TCP sockets, IPv6 TCP sockets, IPv4 UDP datagrams, IPv6 UDP datagrams, Unix-domain stream sockets, and Unix-domain datagrams where the platform supports them. Common helpers include `connect`, `listen`, `accept`, `bind`, `setblocking`, `read`, `write`, `read_all`, `write_all`, `sendto`, `recvfrom`, `valid`, and `close`.

IPv6 uses dedicated socket types while keeping the same method names as IPv4:

- `mxnetwork::SocketType::TYPE_INET6` for IPv6 TCP sockets.
- `mxnetwork::SocketType::TYPE_INET6_DGRAM` for IPv6 UDP sockets.

For example, an IPv6 TCP connection only changes the socket type and address:

```cpp
mxnetwork::Socket socket(mxnetwork::SocketType::TYPE_INET6);
if (!socket.connect("::1", "8080")) {
    std::cerr << "IPv6 connect failed\n";
    return 1;
}
```

For IPv6 UDP, construct `TYPE_INET6_DGRAM`; `bind(port)`, `connect(host, port)`, `sendto`, and `recvfrom` dispatch through MXNetwork's IPv6 datagram helpers.

<a id="project-layout"></a>

## Project Layout

- `mxvk/`
	- Engine source, headers, built-in shaders, and the `mxvk/mxvk_math.h` math helpers used by the 3D math tests.
- `examples/`
	- Runnable examples and sample assets.
- `models/`
	- Model assets used by model-based examples.
- `volk/`
	- Vulkan function loader submodule/source.

## Notes

- The example CMake files copy required runtime assets into each example's output directory after build.


<a id="early-screenshots"></a>

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
