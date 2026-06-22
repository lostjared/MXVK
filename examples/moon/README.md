# Moon

Moon renders a moon model surrounded by a set of small pyramids and a starfield. It is a model-viewer style scene that shows how to combine multiple meshes, a custom fragment shader, and sprite-based space effects.

## Inputs

- `-p <path>` or `--path <path>` - asset root, usually handled automatically by `run.pl`
- `--filename <file>` - optional model override for the moon mesh
- `-S <path>` or `--shader-path <path>` - optional shader override path
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode

## Controls

- `Escape` - quit
- `Space` - toggle automatic spin
- Left mouse drag - orbit the camera around the moon
- Mouse wheel - zoom in or out

## How It Works

The sample loads `moon.obj` and `pyramid.obj` from the example data directory, then renders them with the shared model pipeline from `model_example`. The custom moon fragment shader can be swapped independently through the shader path argument.

The pyramids are positioned relative to the moon rotation so the whole scene reads like a small orbital diorama. A starfield sprite is layered behind the models to give the scene depth without requiring a separate skybox implementation.
