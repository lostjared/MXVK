# Tux Example

<img width="1280" height="720" alt="image" src="https://github.com/user-attachments/assets/9b8e191e-4095-450e-b8aa-0dd5ac744eb5" />

Tux Example loads a textured Tux model and draws it over a moving background sprite. It is a small showcase for combining a model, a full-screen sprite effect, and text overlays in one scene.

## Controls

- `Escape` - quit

## How It Works

The background is rendered from a custom animated sprite shader, then the Tux model is drawn on top with the standard model pipeline. The sample keeps the scene simple on purpose so the focus stays on model loading and layered rendering.

