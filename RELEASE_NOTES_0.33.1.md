# MXVK 0.33.1 Release Notes

Prepared: August 29, 2026

MXVK 0.33.1 is a substantial rendering, capture, and post-processing update
over the last published release, 0.24.0. The releases from 0.25.0 through
0.33.1 strengthen real-time video workflows, add mixed fragment/compute effect
chains, preserve source resolution independently of the preview window, expose
optional shader resources through SPIR-V reflection, and allow a completed
effect chain to become the texture of a 3D model.

## Highlights

- Fragment and compute shaders can be combined in ordered post-processing
  chains with shared uniforms, history textures, audio data, and synchronized
  RGBA8 intermediate images.
- A fixed native render extent separates source-sized effects, text, readback,
  and snapshots from the preview-window resolution.
- Derived renderers can consume the final post-processing image as a non-owning
  sampled texture. `VKAbstractModel` can bind it as texture slot zero, enabling
  processed video and multipass effects to appear directly on model geometry.
- FFmpeg/NVDEC capture supports explicit CUDA device selection, efficient
  decode-and-discard catch-up, reusable decoder contexts for looping video, and
  synchronization for asynchronous copies from decoder-owned surfaces.
- Frame readback is pipelined per frame in flight and prefers host-cached Vulkan
  memory, reducing avoidable stalls in video-writing workflows.

## Rendering and post-processing

### Mixed fragment and compute passes

Version 0.29.0 introduced compute shaders as first-class post-processing
stages. Fragment and compute passes can alternate in one ordered chain while
sharing the extended sprite ABI. MXVK handles intermediate-image layout
transitions and synchronization between graphics and compute work.

### Source-sized rendering and preview composition

Version 0.30.0 separated the native scene/post-processing extent from the
swapchain extent. Shader resolution uniforms, intermediate effects, output
text, and frame readback remain source-sized. The final image is fitted into
the preview while preserving its aspect ratio, and preview-only text can use an
independently selected font size.

Version 0.33.1 completes this path for snapshots. When a fixed render extent is
active and its offscreen post-processing image has been initialized,
`captureSnapshotPixels()` reads that source-sized image instead of the
swapchain. The image is transitioned from shader-read-only layout to transfer
source and restored afterward. The normal swapchain capture path remains the
fallback when no fixed offscreen target is active.

### Text and readback

Version 0.27.0 added preview-only Vulkan text that is drawn after frame
readback, keeping runtime diagnostics out of encoded output. Output text stays
inside the source-sized render path.

Version 0.28.0 moved swapchain presentation and readback synchronization to
per-frame-in-flight resources. Readback copies are pipelined so the CPU consumes
a completed slot while Vulkan records later frames. Version 0.28.1 prefers
host-cached memory where supported and falls back safely when it is unavailable.

## Shader resource reflection and history

Version 0.31.0 added non-owning shared frame-history descriptors for fragment
and compute post-processing. A client can provide one source sprite's history
array to compatible passes without transferring ownership or allocating a
duplicate temporal ring.

SPIR-V inspection identifies the optional set 0 resources used by a shader:

- binding 2: frame-history texture array;
- binding 3: current audio spectrum;
- binding 4: spectrum-history texture.

Version 0.31.1 completed reflection for the spectrum bindings so applications
can allocate and bind every optional resource before pipeline construction.
Shaders that do not declare these bindings continue to use the simpler path.

## 3D model integration

Version 0.32.0 expanded model fragment uniforms with the shared custom-uniform
array, current audio bands, and audio-history values. It also separated the
mutually exclusive vertex and extended-fragment push-constant layouts, avoiding
overlapping stage declarations.

Version 0.33.0 added the post-processing texture-consumer path:

- `VK_Window::setPostProcessingTextureConsumerEnabled()` selects derived-scene
  consumption instead of the normal fullscreen composite;
- `VK_Window::onRecordPostProcessingTexture()` receives the completed sampled
  image view and native extent inside the final rendering scope;
- `VKAbstractModel::renderWithExternalTexture()` binds a non-owning image view
  as model texture slot zero;
- model texture descriptors can be updated for each render without taking
  ownership of the supplied image.

These additions allow fragment, compute, history, and multipass effects to be
evaluated before 3D rendering and mapped onto model UVs instead of appearing as
a screen-space layer over the model.

## Video capture and writing

The capture work introduced across 0.25.0 and 0.26.0 includes:

- explicit CUDA/NVDEC device selection;
- reusable FFmpeg decoder and hardware-device contexts for in-place looping;
- a CUDA event barrier before reusing FFmpeg-owned decoder surfaces;
- decode-and-discard operations for media-clock catch-up without unnecessary
  conversion or upload work.

MXWrite also moves host-frame conversion and upload work away from the render
thread where possible, complementing MXVK's pipelined frame readback. These
changes reduce preview stalls when recording expensive effects or large frames.

## Compatibility and upgrade notes

- Existing conventional fragment shaders remain supported. Compute passes and
  history/audio resources are opt-in and are detected from SPIR-V declarations.
- Existing `VK_Window` subclasses remain source-compatible with the new texture
  consumer hook because the default implementation is a no-op.
- The public C++ class layouts and virtual interface have grown since 0.24.0.
  Treat this as an ABI change: rebuild and relink applications and libraries
  against MXVK 0.33.1 rather than mixing old objects with the new library.
- If MXVK is linked statically, dependent applications must be rebuilt after
  installing the release.
- CUDA interop remains optional. Portable Vulkan and MoltenVK builds can leave
  CUDA disabled.

For a clean CMake rebuild:

```bash
cmake --fresh -S . -B build \
    -DVALIDATION=ON \
    -DCV=ON
cmake --build build -j
sudo cmake --install build
```

Enable optional features such as `WITH_CUDA`, `WITH_MXWRITE`, `MIXER`, and
`JPEG` according to the target system. Existing build directories created for
0.24.0 should be freshly configured before building 0.33.1.

## Version timeline since 0.24.0

- **0.25.0–0.26.0:** CUDA/NVDEC selection, loop reuse, asynchronous decoder
  synchronization, and efficient catch-up decoding.
- **0.27.0:** preview-only post-readback text.
- **0.28.0:** per-frame-in-flight presentation and pipelined readback.
- **0.28.1:** host-cached readback preference and writer-side host-frame work.
- **0.29.0:** ordered fragment/compute post-processing chains.
- **0.30.0:** fixed native render extent and aspect-preserving preview output.
- **0.31.0–0.31.1:** shared history descriptors and optional-resource SPIR-V
  reflection.
- **0.32.0:** extended model custom/audio uniforms and push-constant cleanup.
- **0.33.0:** post-processing texture consumers and external model textures.
- **0.33.1:** source-sized offscreen snapshot capture.

## Validation

The 0.33.1 source tree was configured and built with the portable CMake path
and examples disabled. The generated C++ version header and CMake package
metadata report 0.33.1. Doxygen 1.18.0 generated the HTML documentation without
warnings.
