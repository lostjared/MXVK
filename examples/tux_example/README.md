# Tux Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/a850f2c2-851a-4b2e-9394-88b87facd2d1" />

Tux Example loads a textured Tux model and draws it over a moving background sprite. It is a small showcase for combining a model, a full-screen sprite effect, and text overlays in one scene.

## Controls

- `Escape` - quit

## How It Works

The background is rendered from a custom animated sprite shader, then the Tux model is drawn on top with the standard model pipeline. The sample keeps the scene simple on purpose so the focus stays on model loading and layered rendering.

