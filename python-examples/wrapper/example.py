#!/usr/bin/env python3
## @file example.py
## @brief Minimal application using the simple MXVK wrapper package.

from pathlib import Path

from wrapper import App, Color, Sprite
from wrapper._native import mxvk


class Hello(App):
    ## @brief A small textured-sprite wrapper demonstration.

    def __init__(self) -> None:
        ## @brief Create the window, font, and image sprite.
        root = Path(__file__).resolve().parents[1] / "sprite" / "data"
        super().__init__("MXVK wrapper", 960, 540, vsync=True, shader_directory=root)
        self.set_font(str(root / "font.ttf"), 24)
        self.image = Sprite(self, root / "intro.png", vertex_shader=str(root / "sprite.vert.spv"), fragment_shader=str(root / "fragment.frag.spv"))

    def draw(self) -> None:
        ## @brief Draw the image and a text label.
        width, height = self.swapchain_extent
        self.image.draw(0, 0, width=width, height=height)
        self.print_text("Hello from the simple wrapper", 20, 20, Color(255, 255, 255))

    def on_event(self, event: mxvk.Event) -> None:
        ## @brief Exit when Escape is pressed.
        if event.key_down and event.key == mxvk.KEY_ESCAPE:
            self.quit()


def main() -> None:
    ## @brief Run the wrapper demonstration.
    Hello().run()


if __name__ == "__main__":
    main()
