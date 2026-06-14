# OpenCV Model

OpenCV Model maps a live camera feed onto a textured 3D model. It combines the OpenCV capture path from the camera examples with the model rendering path from the 3D viewer samples.

## Controls

- `Left mouse drag` - orbit the model
- `Mouse wheel` - zoom the camera
- `Left` / `Right` / `Up` / `Down` - manually rotate the model when using the keyboard
- `Escape` - quit

## How It Works

The example opens a camera, uploads each frame into the model texture, and renders the mesh with the current frame visible on the surface. If no arrow key is held, the model slowly auto-spins, so the scene keeps moving even with no input.

