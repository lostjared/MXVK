# Shader Viewer

Shader Viewer displays a camera, video, or textured model through sprite-compatible fragment shaders. It can browse compiled SPIR-V shader effects against live OpenCV input or map the effects over a model's UV coordinates.

## Controls

- `Up` / `Down` - switch shaders
- Mouse position and left-button state - passed to shader uniforms
- `Escape` - quit

In model mode, drag or use `Left` / `Right` to rotate, use the wheel or `A` / `S` to zoom, `W` to toggle wireframe, `R` to toggle auto-rotation, and `Home` to reset the view.

## Inputs

- Requires building with `-DCV=ON`
- `--camera <index>` - OpenCV camera index
- `--filename <file>` - video file instead of a camera; with `--model`, use the video as the model texture
- `--model <file>` - load an `.obj`, `.mxmod`, or `.mxmod.z` model instead of opening a capture source
- `--texture <file>` - optional model texture manifest or image
- `--resource_path <dir>` - optional base directory for model textures
- `--shader-path <dir>` - directory containing `index.txt`
- `--shader-index <index>` - initial shader entry

## How It Works

The shader index may list compiled `.spv` files directly, or `.glsl` source names when matching compiled files exist under a `spv/` subdirectory. Capture mode uploads each frame to a sprite and enables extended shader uniforms. Model mode uses the model's UV coordinates and material texture at binding 0, while supplying the same sprite-compatible resolution and time push constants to the fragment shader. When `--model` and `--filename` are combined, each decoded video frame replaces texture slot 0 and is mapped across the entire model before the selected fragment shader runs.
