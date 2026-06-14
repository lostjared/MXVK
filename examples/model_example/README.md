# Model Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/29f6a563-4354-4da8-8eaa-50944d9c6c63" />

This is a basic 3D model viewer for MXVK. By default it loads `data/pyramid.obj`, rotates it slowly, and renders it with a simple perspective camera.

## Controls

- `Escape` - quit

## How It Works

The model is loaded through `VKAbstractModel` along with its texture manifest and texture directory. Each frame updates a model/view/projection UBO, then draws the mesh with the example's model shaders. It is the simplest reference for loading and displaying a textured OBJ or MXMOD asset.

