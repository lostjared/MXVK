# Shader Viewer

Shader Viewer displays a camera or video source through sprite-compatible fragment shaders. It is intended for browsing a directory of compiled SPIR-V shader effects against live OpenCV input.

## Controls

- `Up` / `Down` - switch shaders
- Mouse position and left-button state - passed to shader uniforms
- `Escape` - quit

## Inputs

- Requires building with `-DCV=ON`
- `--camera <index>` - OpenCV camera index
- `--filename <file>` - video file instead of a camera
- `--shader-path <dir>` - directory containing `index.txt`
- `--shader-index <index>` - initial shader entry

## How It Works

The shader index may list compiled `.spv` files directly, or `.glsl` source names when matching compiled files exist under a `spv/` subdirectory. The example uploads each captured frame to a sprite and enables extended shader uniforms for time, frame timing, resolution, and mouse state.

