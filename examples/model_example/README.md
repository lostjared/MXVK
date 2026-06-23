# Model Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/29f6a563-4354-4da8-8eaa-50944d9c6c63" />

This is a basic 3D model viewer for MXVK. By default it loads `data/pyramid.obj` from the example asset directory and renders it with a simple perspective camera.

### Arguments

- `--filename <file>` - override the mesh file. The example accepts OBJ and MXMOD assets, including compressed `.mxmod.z` files. If you omit it, the example uses the bundled pyramid model.
- `--resource <file>` - optional texture manifest for custom models. Use this when the model needs a manifest that is separate from the mesh file itself.
- `--resource_path <dir>` - optional base directory for texture lookup. This is useful when your manifest uses relative texture paths.
- `--binary` - replace the model's primary texture with animated Matrix-style green rain. This also disables backface culling so the inside of the mesh can be viewed.
- `--font-size <px>` - adjust the rain glyph raster size when `--binary` is enabled.
- `--font-path <file>` - override the rain font file when `--binary` is enabled.
- `--color <spec>` - tint the rain when `--binary` is enabled using `#RRGGBB` or `R,G,B`.

## Controls

- `Escape` - quit
- `Left mouse drag` - rotate the model
- `Space` - toggle the model's automatic spin
- `Mouse wheel` - zoom in or out
- `Enter` - toggle skybox mode
- `W/A/S/D` - look around inside the skybox view

## How It Works

The model is loaded through `VKAbstractModel` along with its texture manifest and texture directory. Each frame updates a model/view/projection UBO, then draws the mesh with the example's model shaders. Mouse drag adjusts the model orientation, the wheel changes camera distance, and the automatic Y-axis spin can be toggled with `Space`.

When `--filename` is omitted, `model_example` uses the bundled `data/pyramid.obj`. When `--filename` is provided, the argument replaces that default mesh and lets you point the viewer at your own OBJ or MXMOD asset. If the model has external textures, pair it with `--resource` and `--resource_path` so the loader can find the manifest and the image files.

When `--binary` is enabled, the example renders Matrix-style rain into an off-screen SDL surface and uploads it into the model's primary texture every frame. The bundled default font is the bold `Noto Sans CJK JP` face; `--font-path <file>` can point the rain at another font. The geometry does not change, but the surface texture is replaced live, and backface culling is disabled so the model can be viewed from the inside.

Pressing `Enter` switches between the normal orbit view and an inside-the-mesh skybox view that reuses the same `--filename` model. This works whether or not `--binary` is enabled. When binary mode is off, the mesh keeps its normal texture; when it is on, the rain texture remains active in both views. `W/A/S/D` adjust the viewing direction in skybox mode.
