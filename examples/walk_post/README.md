# Walk Post

<img width="1280" height="720" alt="vlcsnap-2026-07-05-05h04m18s318" src="https://github.com/user-attachments/assets/a7adaf8f-182e-4862-afda-a22677b27640" />

`walk_post` is a first-person maze and post-processing shader browser. It keeps the procedural walls, pillars, collectibles, projectiles, gamepad support, and debug console from the `walk` sample, then renders the scene through a selectable full-screen post-processing shader.

Use it when testing fragment effects that need real scene input, mouse state, timing, frame count, and resolution uniforms instead of a static quad.

## Run

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j
./run.pl walk_post --shader-path /path/to/post_fx --shader-index 0
```

`--shader-path` should point at a directory containing `index.txt`. Each non-empty, non-comment entry in that file should name a compiled `.spv` fragment shader. Entries may be direct `.spv` names, source-style names that resolve to matching `.spv` files, or files in a `spv/` subdirectory.

If `--shader-path` is omitted, the maze still runs, but no post-processing effect is attached.

## Controls

- `W` / `A` / `S` / `D` - move
- `Mouse` or right stick - look around
- `Left Shift` or left stick click - sprint
- `Left Ctrl` - crouch
- `Space` or gamepad south - jump
- `Left mouse click` or right shoulder - fire a projectile
- `R` / `T` - previous/next post-processing shader
- `F` - toggle the local FPS overlay
- `F3` - open or close the debug console
- `Escape` - release mouse capture, or quit if capture is already released
- `Back` / `Start` on a gamepad - quit
- Double-click the left mouse button - recapture the mouse

## Console

The inherited `VK_IOWindow` console exposes runtime tools for testing world state and shaders. Use the console help command to print the complete command list. `walk_post` adds or uses these commands heavily:

- `spawn_random [attempts]` - move the player to a random valid location
- `reset` - reactivate and relocate all collectibles
- `add_collectibles [count]` - add random collectible objects
- `status` - print camera, world, projectile, and particle state
- `teleport <x> <y> <z>` - move the camera if the target does not collide
- `clear_bullets` / `clear_fx` - clear active projectiles or explosion particles
- `set_fps <on|off>` - set the local FPS overlay
- `set_wall`, `set_floor`, `set_pillar`, `set_object`, `set_bullet` - reload scene fragment shaders
- `list_shaders` - print available scene shaders and the current post-processing shader
- `regen_world [seed]` - regenerate the maze, pillars, and collectibles

## Shader Notes

The example compiles its scene shaders from `examples/walk_post/shaders` into the build tree. Runtime scene shader commands resolve names such as `wall.frag.spv`, `floor_swirl.frag.spv`, or `floor_twist.frag.spv` from that compiled shader directory first.

Post-processing shaders are loaded from the user-provided `--shader-path` index and attached with `VK_Window::attachPostProcessingShader(...)`. The post-processing sprite enables extended uniforms and updates elapsed time, frame delta, frame count, mouse state, and frame-rate values every frame, which makes it useful for Shadertoy-style effects adapted to MXVK's sprite shader interface.

## Assets

`examples/walk_post/data` contains the sample models, manifests, textures, font, and runtime sprite/text shader assets used by the scene. The CMake target copies those assets and the compiled scene shaders into the example output directory during the build.
