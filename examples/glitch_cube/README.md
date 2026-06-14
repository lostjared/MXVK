# Glitch Cube

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/b73dd23b-4226-41a6-b734-3157dae5fd37" />

Glitch Cube is a stylized 3D model viewer that renders a textured cube with a time-based transform and MXVK text overlays. It is intended as a compact example of model loading, camera control, and shader-driven presentation.

## Controls

- `Left mouse drag` - orbit the camera around the cube
- `Mouse wheel` - zoom in or out
- `Space` - toggle the automatic rotation axis
- `Page Up` / `Page Down` - scale the cube
- `Escape` - quit

## How It Works

The cube is loaded through `VKAbstractModel` from the compressed model and texture manifest data in `data/`. The example updates a model/view/projection UBO every frame, then renders the model with custom shaders while the background and text are drawn through the normal MXVK sprite and text paths.

