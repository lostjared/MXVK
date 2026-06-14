# Starship Example

This example loads `data/starship.obj` with its material file and texture set, then renders it as a rotating 3D ship with a moving starfield behind it.

The ship uses the example's model shaders for textured Phong shading. The starfield is rendered as a separate point-sprite layer, and the engine exhaust is drawn as a small 3D cone aligned to the rear jet.

## Controls

- `Escape` - quit
- Mouse drag - rotate the ship

## Assets

The example expects these files in `data/`:

- `starship.obj` - the ship mesh
- `starship.mtl` - material definitions for the mesh
- `ship_hull.png` - main hull texture
- `ship_wing.png` - wing texture
- `ship_fin.png` - fin texture
- `ship_engine.png` - engine texture
- `ship_cockpit.png` - cockpit texture
- `ship_cockpit_red.png` - red cockpit variant
- `rear_cap_metal.png` - rear cap material texture
- `plane.png` - auxiliary texture used by the mesh
- `star.png` - starfield texture copied from the Pong example

## How It Works

The example loads the OBJ through `VKAbstractModel`, which also handles the material-to-texture mapping from the MTL file. Each frame:

- updates the starfield simulation and draws it first
- updates the ship model/view/projection UBO
- renders the ship with Phong lighting
- draws the flame cone with depth testing and alpha blending so it looks like a game-style exhaust plume

This example is a compact reference for textured model loading, mouse-driven rotation, and layering 2D/3D effects in MXVK.
