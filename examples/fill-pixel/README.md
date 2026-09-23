# fill_pixel

This headless example decodes two videos with `VK_FF_Capture`, renders each pair
through the fragment shader, and encodes the result with MXWrite. The source
video is sampler binding 0. The material video is sampler binding 6. The output
uses the source video's dimensions and frame rate; decoding stops when either
input ends. Both inputs must keep constant dimensions.

Build with FFmpeg/MXWrite support enabled:

```bash
cmake -S . -B build -DWITH_MXWRITE=ON
cmake --build build -j --target fill_pixel
```

Run from the repository root:

```bash
./build/examples/fill-pixel/fill_pixel source.mp4 material.mp4 output.mp4 1 0
```

The optional arguments are `alpha` (default `1`) and `restore-black` (`0` or
`1`, default `0`). When enabled, exact opaque black source pixels are discarded.
Other source channels above `0.6` are multiplied by the corresponding material
channel and `alpha`.
