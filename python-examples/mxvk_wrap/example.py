#!/usr/bin/env python3
## @file example.py
## @brief Minimal application using the @c mxvk_wrap package.

from pathlib import Path
import sys

# Support both `python3 python-examples/mxvk_wrap/example.py` and running this
# file directly from its own directory.
EXAMPLES_DIR = Path(__file__).resolve().parents[1]
if str(EXAMPLES_DIR) not in sys.path:
    sys.path.insert(0, str(EXAMPLES_DIR))

import mxvk_wrap as mx


class Hello(mx.App):
    ## @brief A small textured-sprite wrapper demonstration.

    def __init__(self) -> None:
        ## @brief Create the window, font, and image sprite.
        root = Path(__file__).resolve().parents[1] / "sprite" / "data"
        super().__init__("MXVK wrapper", 960, 540, vsync=True, shader_directory=root)
        self.set_font(str(root / "font.ttf"), 24)
        self.image = mx.Sprite(self, root / "intro.png", vertex_shader=str(root / "sprite.vert.spv"), fragment_shader=str(root / "fragment.frag.spv"))

    def draw(self) -> None:
        ## @brief Draw the image and a text label.
        width, height = self.swapchain_extent
        self.image.draw(0, 0, width=width, height=height)
        self.print_text("Hello from the simple wrapper", 20, 20, mx.Color(255, 255, 255))

    def on_event(self, event) -> None:
        ## @brief Exit when Escape is pressed.
        if event.key_down and event.key == mx.KEY_ESCAPE:
            self.quit()


def main() -> None:
    ## @brief Run the wrapper demonstration.
    Hello().run()


if __name__ == "__main__":
    main()
