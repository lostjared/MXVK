"""Blue Matrix rain using cached CJK glyphs and an app-owned dynamic texture."""
import math
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import mxvk_wrap as mx


class MatrixRain:
    def __init__(self, app, font_path, fragment_shader, vertex_shader):
        self.sprite = mx.Sprite(app, width=1280, height=720, vertex_shader=str(vertex_shader), fragment_shader=str(fragment_shader))
        self.rng = random.Random()
        font = ImageFont.truetype(str(font_path), 22)
        symbols = [chr(code) for code in range(0xFF66, 0xFF9E) if code != 0xFF70]
        self.cell_width, self.cell_height = 16, 34
        self.rows = math.ceil(720 / self.cell_height)
        self.glyphs = []
        for symbol in symbols:
            levels = []
            for level in range(9):
                t = min(1, level / 7)
                color = (int(47 * t), int(141 * t), int(255 * t), 255 if level == 8 else int(95 + 140 * t))
                glyph = Image.new("RGBA", (self.cell_width, self.cell_height))
                ImageDraw.Draw(glyph).text((2, 0), symbol, font=font, fill=color)
                levels.append(glyph)
            self.glyphs.append(levels)
        self.streams = [self.new_stream(True) for _ in range(math.ceil(1280 / self.cell_width))]
        self.canvas = np.zeros((720, 1280, 4), dtype=np.uint8)
        self.frame = 0

    def new_stream(self, initial=False):
        rows = self.rows
        return [self.rng.uniform(-rows * (1.5 if initial else 0.8), 0 if initial else -1), self.rng.uniform(9, 26 if initial else 27), min(rows if initial else rows + rows // 4, self.rng.randint(12 if initial else 10, 38 if initial else 42)), self.rng.randrange(len(self.glyphs)), self.rng.randrange(1201)]

    def update(self, delta, opacity):
        fade = 0.5 ** (min(delta, 1 / 15) * 8)
        self.canvas = (self.canvas * np.array((fade * 0.72, fade, fade * 0.55, fade * opacity))).astype(np.uint8)
        image = Image.fromarray(self.canvas)
        for column, stream in enumerate(self.streams):
            stream[0] += stream[1] * min(delta, 1 / 15)
            if stream[0] - stream[2] > self.rows + 3:
                stream = self.streams[column] = self.new_stream()
            head, _, length, offset, shimmer = stream
            for tail in range(length, -1, -1):
                row = math.floor(head) - tail
                if not 0 <= row < self.rows:
                    continue
                level = 8 if tail == 0 else 7 if tail <= 2 else max(0, 7 - round(tail / max(1, length) * 8))
                index = (offset + row * 17 + column * 11 + shimmer + self.frame // 3 + tail * 5) % len(self.glyphs)
                x, y = column * self.cell_width, row * self.cell_height
                image.paste((0, 0, 0, 0), (x, y, min(1280, x + self.cell_width), min(720, y + self.cell_height)))
                image.alpha_composite(self.glyphs[index][level], (x, y))
        self.canvas = np.array(image)
        self.sprite.update(self.canvas, 1280, 720)
        self.sprite.shader_params(0, 0, 0, opacity)
        self.frame += 1
