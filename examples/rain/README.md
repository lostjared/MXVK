# Rain

Rain is a reusable helper library for Matrix-style glyph rain. It is built as a static library and is used by examples such as `matrix`, `binary_matrix`, `model_example`, `planet`, `defender`, and `puzzle_drop`.

## Controls

- `Space` - reset/randomize rain streams when a host example forwards events

## How It Works

The library loads glyphs with `SDL_ttf`, renders configurable full or binary symbol streams into an SDL surface, and synchronizes that surface into an MXVK sprite texture. It is not normally launched directly with `run.pl`.

