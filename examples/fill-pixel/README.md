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
./build/examples/fill-pixel/fill_pixel --input source.mp4 --fill material.mp4 --output output.mp4 --alpha 1 --restore-black 0 --codec libx264 --bitrate 10000000 --preset fast --tune film
```

The executable loads MXVK shaders from `data/` and the fill-pixel shader from
`shaders/` beside the executable, regardless of the working directory. In a
Windows install, these directories are under `bin/fill_pixel/` alongside
`fill_pixel.exe`.

`--input`, `--fill`, and `--output` are required. The optional `--alpha`
defaults to `1`, and `--restore-black` accepts `0` or `1` (default `0`).
When enabled, exact opaque black source pixels are discarded.
Other source channels above `0.6` are multiplied by the corresponding material
channel and `alpha`.

Use `--codec`, `--bitrate`, `--preset`, and `--tune` to configure MXWrite.
The codec defaults to `auto`, the preset to `medium`, and the tune to the
encoder default. Bitrate is in bits per second; `0` (the default) uses
MXWrite's CRF/CQ setting. Run `fill_pixel --help` for the option list.
Progress appears on stderr as encoded frames, approximate percentage when
both input containers report frame counts, and processing frames per second.
