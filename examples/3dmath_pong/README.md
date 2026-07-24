# 3D Math Pong

`3dmath_pong` is a software-rasterized 3D Pong game. Geometry transforms use
the MXVK math backend selected at configure time, while the CPU rasterizer
draws into a 320x240 framebuffer. MXVK presents that framebuffer in a
1440x1080 Vulkan window with nearest-neighbor scaling. Pass
`--framebuffer WIDTHxHEIGHT` to override the default internal resolution.

Build and run:

```bash
cmake -S . -B build
cmake --build build --target 3dmath_pong -j
./run.pl 3dmath_pong
./run.pl 3dmath_pong --framebuffer 640x480
```

Controls:

- `W`/`S` or `Up`/`Down`: move the player paddle
- Mouse: move the player paddle
- `Space`: pause or resume
- `R`: reset the match
- `Escape`: quit

Configure with `-DWITH_EIGEN=OFF` to use the native MXVK math implementation,
or `-DWITH_EIGEN=ON` to use the Eigen-backed implementation.
