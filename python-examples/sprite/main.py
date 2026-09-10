#!/usr/bin/env python3

import argparse
from pathlib import Path
import mxvk_ext as mxvk

EXAMPLE_DIR = Path(__file__).resolve().parent
DATA_DIR = EXAMPLE_DIR / "data"

class SpriteWindow(mxvk.IOWindow):
    def __init__(self, width: int, height: int, fullscreen: bool, enable_vsync: bool) -> None:
        super().__init__(str(EXAMPLE_DIR), "VK_Example", width, height, fullscreen, enable_vsync)
        self.fallback_width = width
        self.fallback_height = height
        self.set_font(str(DATA_DIR / "font.ttf"), 24)
        self.sprite = self.create_sprite(str(DATA_DIR / "intro.png"), str(DATA_DIR / "sprite.vert.spv"), str(DATA_DIR / "fragment.frag.spv"))

    def close(self) -> None:
        # Sprite is owned by the native window and its Python handle keeps this
        # window alive. Drop the handle first so the two objects cannot form a
        # shutdown-time reference cycle.
        self.sprite = None
        self.release()

    def proc(self) -> None:
        width, height = self.swapchain_extent
        if width <= 0 or height <= 0:
            width = self.fallback_width
            height = self.fallback_height

        self.sprite.draw_rect(0, 0, width, height)
        self.print_text("Hello, World!", 15, 15, mxvk.Color(255, 255, 255, 255))

    def console_proc(self) -> None:
        pass

    def console_event(self, event: mxvk.Event) -> None:
        if event.type == mxvk.EVENT_KEY_DOWN and event.key == mxvk.KEY_ESCAPE:
            self.request_exit()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MXVK Python sprite example")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--vsync", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    window = SpriteWindow(args.width, args.height, args.fullscreen, args.vsync)
    try:
        window.loop()
    finally:
        window.close()

if __name__ == "__main__":
    main()
