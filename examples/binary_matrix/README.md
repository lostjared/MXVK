# Binary Matrix

Binary Matrix is a 3D variant of the Matrix rain demo. It renders `0` and `1` glyphs as depth-aware sprites over a shader-driven background, then places the result inside an orbital camera scene.

## Inputs

- `-p <path>` or `--path <path>` - asset root, usually handled automatically by `run.pl`
- `-r <WxH>` or `--resolution <WxH>` - window resolution
- `-f` or `--fullscreen` - fullscreen mode
- `-z <px>` or `--font-size <px>` - set the font size and spacing between glyphs
- `--color <spec>` - tint the rain glyphs using `#RRGGBB` or `R,G,B`

## Controls

- `Escape` - quit
- `Space` - randomize the rain streams
- `Left` / `Right` - orbit the camera around the scene
- `Up` / `Down` - tilt the camera
- `Page Up` / `Page Down` - zoom the camera
- Mouse movement and button presses - feed the animated background shader

## How It Works

The example renders a field of independent rain streams. Each stream chooses between `0` and `1` glyphs, advances at its own speed, and fades through a short trail so the columns feel layered instead of flat.

The digits are rendered with `SDL_ttf` into off-screen surfaces, converted into `VK_Sprite3D` batches, and drawn inside a 3D camera. A separate background sprite receives the mouse position and button state every frame so the backdrop reacts to input even though the main scene is mostly keyboard-driven. The glyph tint can be overridden with `--color`.
