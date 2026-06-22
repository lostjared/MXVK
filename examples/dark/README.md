# Dark Crystal Pyramid

Dark Crystal Pyramid renders a stylized pyramid scene with a dramatic beam effect, animated title text, and a textured 3D model. It is a compact example of layering model rendering with text and custom shader passes.

## Inputs

- `-p <path>` or `--path <path>` - asset root, usually handled automatically by `run.pl`
- `--filename <file>` - optional model override
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode

## Controls

- `Escape` - quit
- Left mouse drag - orbit the camera
- Mouse wheel - zoom in or out

## How It Works

The example loads a pyramid model and a beam model, then renders both with custom shaders. The beam is alpha blended, so it reads as a translucent effect layered over the main object instead of a separate opaque mesh.

The title text is drawn in `proc()` so it remains tied to the current window size. Mouse drag changes the camera orientation, the wheel adjusts distance, and the scene keeps a slow auto-spin so it remains animated even without input.
