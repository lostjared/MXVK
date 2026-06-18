# Compute Shader Camera Effects

This example captures frames from either a camera or a video file, runs them through a Vulkan compute pipeline, displays the processed output, and records the source stream through MXWrite. The shader list is read from `data/index.txt`, so the same executable can swap between several compute effects without rebuilding.

Use `--filename <file>` for video playback. If `--filename` is not provided, the example falls back to `--camera <index>`. Passing `-r/--resolution` sets the compute canvas and recorded video size; source frames are scaled into that canvas before shader processing. When playing a file without `-r/--resolution`, the window auto-resizes to the video frame size unless fullscreen is enabled. Use `--output <file>` and `--crf <value>` to control the MXWrite recording output; when omitted, the example writes `compute_shader_output.mp4` with CRF `24` into the example asset directory.

## Controls

- `Up` - load the previous compute shader from `index.txt`
- `Down` - load the next compute shader from `index.txt`
- `Left` / `Right` - change the 50-mode selector inside `acidcam_filters.spv`
- `Escape` - quit

## How It Works

Frames arrive from OpenCV, then get uploaded into a Vulkan storage image. If recording is enabled, the source frames are also written out with MXWrite. The selected compute shader processes the image, and the result is drawn back to the swapchain with a simple full-screen presentation pass.
