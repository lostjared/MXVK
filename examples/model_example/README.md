# Model Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/29f6a563-4354-4da8-8eaa-50944d9c6c63" />

This is a basic 3D model viewer for MXVK. By default it loads `data/pyramid.obj` and renders it with a simple perspective camera.

## Controls

- `Escape` - quit
- `Left mouse drag` - rotate the model
- `Space` - toggle the model's automatic spin
- `Mouse wheel` - zoom in or out
- `--binary` - replace the model texture with matrix-style green rain
- `Enter` - toggle skybox mode when `--binary` is enabled
- `W/A/S/D` - look around inside the skybox view

## How It Works

The model is loaded through `VKAbstractModel` along with its texture manifest and texture directory. Each frame updates a model/view/projection UBO, then draws the mesh with the example's model shaders. Mouse drag adjusts the model orientation, the wheel changes camera distance, and the automatic Y-axis spin can be toggled with `Space`. When `--binary` is enabled, the example renders the matrix rain into an off-screen SDL surface and uploads it into the model's primary texture every frame. Pressing `Enter` switches between the normal orbit view and an inside-the-mesh skybox view that reuses the same `--filename` model, and `W/A/S/D` adjust the viewing direction in that skybox mode.
