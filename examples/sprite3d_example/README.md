# Sprite3D Example

Sprite3D Example shows how to build a 3D scene out of sprites instead of meshes. It combines a starfield, a flying saucer sprite, and an orbiting camera to demonstrate the 3D sprite path in MXVK.

## Inputs

- `-p <path>` or `--path <path>` - asset root, usually handled automatically by `run.pl`
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode

## Controls

- `Escape` - quit
- Left mouse drag - orbit the camera
- Mouse wheel - zoom in or out

## How It Works

The example creates a generated star texture for the background and loads a transparent saucer sprite from the example data directory. Both are converted into `VK_Sprite3D` batches and drawn into the same scene.

The stars are generated procedurally with orbit rings, twinkle variation, and depth layering. The saucer instances move along their own flight paths so the scene feels populated even though it is built entirely from sprite primitives.
