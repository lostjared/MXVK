# MXVK 0.35.0 Release Notes

MXVK 0.35.0 adds compute-shader processing and encoded GPU readback to the
Python video workflow. The `opencv_mxwrite_shader` example now feeds captured
video into an MXVK compute post-processing chain and writes the processed RGBA
frames through MXWrite, while preserving source timestamps.

The nanobind `VK_Window` wrapper exposes `on_frame_readback`,
`on_frame_readback_scheduled`, and `flush_frame_readbacks()` so Python recorders
can consume completed Vulkan frames safely. MXVK Python wheels now enable
OpenCV support by default (`CV=ON`); pass
`-Ccmake.define.CV=OFF` to pip to build without the OpenCV capture API.

See the root README for Linux package prerequisites, virtual-environment setup,
wheel configuration overrides, and the complete MXVK/MXWrite installation
workflow.
