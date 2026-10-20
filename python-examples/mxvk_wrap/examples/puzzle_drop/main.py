#!/usr/bin/env python3
"""The native 3D Puzzle Drop example, translated to mxvk_wrap."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import random
import sys
import time

import numpy as np

EXAMPLES_DIR = Path(__file__).resolve().parents[3]
if str(EXAMPLES_DIR) not in sys.path:
    sys.path.append(str(EXAMPLES_DIR))

import mxvk_wrap as mx

if __package__:
    from .compile_shaders import compile_shaders
    from .game import BOARD_HEIGHT, BOARD_WIDTH, CLEAR, FALL_SECONDS, MATCH, NULL, PuzzleGame
    from .matrices import look_at, perspective, rotation_x, rotation_y, scale, translation
    from .rain import MatrixRain
else:
    from compile_shaders import compile_shaders
    from game import BOARD_HEIGHT, BOARD_WIDTH, CLEAR, FALL_SECONDS, MATCH, NULL, PuzzleGame
    from matrices import look_at, perspective, rotation_x, rotation_y, scale, translation
    from rain import MatrixRain

KEY = {name: mx.native.key_code(name) for name in ("Return", "Keypad Enter", "Space", "Escape", "1", "2", "3", "Left", "Right", "Up", "Down", "Z", "X", "W", "A", "S", "D", "PageUp", "PageDown")}
CONFIRM = (KEY["Return"], KEY["Keypad Enter"], KEY["Space"])
CUBE_SCALE = 0.048
CUBE_SPACING = CUBE_SCALE * 1.12
BLOCK_FILES = ("red1", "red2", "red3", "green1", "green2", "green3", "blue1", "blue2", "blue3", "red3")
BLOCK_TINTS = ((1, .48, .42), (1, .58, .50), (1, .72, .62), (.42, .95, .55), (.55, 1, .65), (.70, 1, .78), (.38, .62, 1), (.50, .74, 1), (.66, .86, 1), (1, .92, .35))


class RepeatInput:
    """Independent held-input timers, with a configurable first repeat delay."""

    def __init__(self):
        self.states = {}

    def update(self, name, value, delta, initial, interval, action, immediate=True):
        previous, timer = self.states.get(name, (0, 0))
        if not value:
            self.states[name] = (0, 0)
            return
        if value != previous:
            timer = initial
            if immediate:
                action(value)
        else:
            timer -= delta
            while timer <= 0:
                action(value)
                timer += interval
        self.states[name] = (value, timer)


class PuzzleDropWindow(mx.App):
    def __init__(self, width=1280, height=720, fullscreen=False, vsync=True, validation=False):
        compile_shaders()
        self.root = Path(__file__).resolve().parent
        self.data, self.shaders = self.root / "data", self.root / "shaders"
        self.cube = self.backdrop = self.controller = None
        self.buttons = set()
        self.controller_check = 0
        self.game = PuzzleGame()
        self.repeat_input = RepeatInput()
        self.intro_active, self.intro_fade = True, 1.0
        self.game_started = False
        self.grid_yaw, self.grid_pitch, self.camera_distance = -10.0, -8.0, 2.32
        self.intro_started = self.intro_updated = self.last_frame = self.last_fall = self.last_process = time.monotonic()
        self.background_time = 0.0
        self.wildcard_color = (1, 0, 1)
        super().__init__("MXVK Wrap 3D Puzzle Drop", width, height, fullscreen=fullscreen, vsync=vsync, validation=validation)
        try:
            self.set_clear_color(.03, .04, .05, 1)
            self.font = self.add(mx.Font(self.data / "font.ttf", 22))
            vertex = str(self.shaders / "sprite.vert.spv")
            images = list(range(1, 9))
            random.shuffle(images)
            self.backgrounds = [mx.Sprite(self, self.data / f"level{i}.png", vertex_shader=vertex, fragment_shader=str(self.shaders / "puzzle_drop_background.frag.spv")) for i in images]
            self.intro = mx.Sprite(self, self.data / "intro1.png", vertex_shader=vertex, fragment_shader=str(self.shaders / "intro.frag.spv"))
            self.rain = MatrixRain(self, self.data / "NotoSansCJK-Bold.ttc", self.shaders / "rain.frag.spv", self.shaders / "sprite.vert.spv")
            self.border = mx.Sprite(self, width=1, height=1)
            self.border.update(np.full((1, 1, 4), 255, dtype=np.uint8), 1, 1)
            self.preview_blocks = [mx.Sprite(self, self.data / f"{name}.png") for name in BLOCK_FILES]
            self.cube = self.load_model("cube_textures.txt")
            self.backdrop = self.load_model("manifest_gray.txt")
            self.backdrop.native.set_alpha_blending(True)
            self.sync_controller()
        except Exception:
            self.close()
            raise

    def load_model(self, manifest):
        model = mx.Model(self, self.data / "cube.mxmod.z", textures=self.data / manifest, texture_directory=self.data)
        model.shaders(self, self.shaders / "puzzle_drop_piece.vert.spv", self.shaders / "puzzle_drop_piece.frag.spv")
        return model

    def on_swapchain_recreated(self):
        for model in (self.cube, self.backdrop):
            if model is not None and model.native is not None:
                model.native.resize(self)

    def reset_game(self):
        self.game.reset()
        self.repeat_input.states.clear()
        self.last_fall = self.last_process = time.monotonic()
        self.game_started = True

    def skip_intro(self):
        self.intro_fade = min(self.intro_fade, .02)
        self.intro_updated = time.monotonic() - .036

    def on_event(self, event):
        if not event.key_down or event.repeat:
            return
        key = event.key
        if key == KEY["Escape"]:
            self.quit()
        elif self.intro_active and key in CONFIRM:
            self.skip_intro()
        elif key in (KEY["1"], KEY["2"], KEY["3"]):
            self.game.difficulty = (KEY["1"], KEY["2"], KEY["3"]).index(key)
            self.reset_game()
        elif key in CONFIRM and self.game.game_over:
            self.reset_game()
        elif self.game_started and not self.game.game_over:
            if key == KEY["Z"]:
                self.game.rotate(-1)
            elif key == KEY["X"]:
                self.game.rotate(1)

    def sync_controller(self):
        if self.controller is not None and not self.controller.native.active():
            self.controller.close()
            self.controller = None
            self.buttons.clear()
            for name in tuple(self.repeat_input.states):
                if name.startswith("pad_"):
                    del self.repeat_input.states[name]
        if self.controller is None and mx.Controller.count() > 0:
            controller = mx.Controller()
            if controller.native.active():
                self.controller = self.add(controller)
            else:
                controller.close()

    def drop(self, hard=False):
        if hard:
            self.game.hard_drop()
        else:
            self.game.drop()
        self.last_fall = time.monotonic()

    def poll_gamepad(self, now, delta):
        if now - self.controller_check >= 1:
            self.controller_check = now
            self.sync_controller()
        if self.controller is None:
            return
        buttons = {i for i in range(15) if self.controller.button(i)}
        pressed = buttons - self.buttons
        self.buttons = buttons
        for button in sorted(pressed):
            if self.intro_active:
                if button in (0, 6):
                    self.skip_intro()
            elif self.game.game_over:
                if button in (0, 6):
                    self.reset_game()
                elif button == 4:
                    self.quit()
            elif self.game_started:
                if button in (0, 2):
                    self.game.rotate(1 if button == 0 else -1)
                elif button == 1:
                    self.drop(hard=True)
                elif button == 4:
                    self.quit()
        if self.intro_active or not self.game_started or self.game.game_over:
            return
        left_x, left_y, right_x, right_y = [self.controller.axis(i) for i in range(4)]
        direction = (-1 if left_x < -10000 else 1 if left_x > 10000 else 0) if (13 in buttons) == (14 in buttons) else (-1 if 13 in buttons else 1)
        self.repeat_input.update("pad_move", direction, delta, .22, .12, self.game.move)
        self.repeat_input.update("pad_down", 12 in buttons or left_y > 10000, delta, .18, .08, lambda _: self.drop(), immediate=False)
        self.repeat_input.update("pad_cycle", 11 in buttons, delta, .16, .11, lambda _: self.game.piece.cycle(), immediate=False)
        if abs(right_x) > 10000:
            self.grid_yaw += right_x / 32768 * 120 * delta
        if abs(right_y) > 10000:
            self.grid_pitch = max(-70, min(70, self.grid_pitch - right_y / 32768 * 100 * delta))
        self.camera_distance = max(1.35, min(7, self.camera_distance + ((9 in buttons) - (10 in buttons)) * 3.2 * delta))

    def poll_keyboard(self, delta):
        held = lambda name: mx.key_pressed(KEY[name])
        self.grid_yaw += (held("D") - held("A")) * 115 * delta
        self.grid_pitch = max(-70, min(70, self.grid_pitch + (held("W") - held("S")) * 90 * delta))
        self.camera_distance = max(1.35, min(7, self.camera_distance + (held("PageDown") - held("PageUp")) * 3.2 * delta))
        if not self.game_started or self.game.game_over:
            self.repeat_input.states.clear()
            return
        direction = int(held("Right")) - int(held("Left"))
        self.repeat_input.update("key_move", direction, delta, .16, .065, self.game.move)
        self.repeat_input.update("key_down", held("Down"), delta, .045, .045, lambda _: self.drop())
        self.repeat_input.update("key_cycle", held("Up"), delta, .16, .11, lambda _: self.game.piece.cycle())

    def draw(self):
        now = time.monotonic()
        delta = max(0, now - self.last_frame)
        self.last_frame = now
        self.wildcard_color = tuple(random.randrange(255) / 255 for _ in range(3))
        if self.intro_active:
            if now - self.intro_updated > .035:
                self.intro_updated = now
                self.intro_fade -= .01
            if self.intro_fade <= 0:
                self.intro_active = False
                self.intro_fade = 0
                self.game_started = True
                self.last_fall = self.last_process = now
                self.repeat_input.states.clear()
            else:
                self.rain.update(delta, self.intro_fade)
        else:
            self.background_time += delta
            self.poll_keyboard(delta)
        self.poll_gamepad(now, delta)
        if self.game_started and not self.game.game_over:
            if now - self.last_fall >= FALL_SECONDS[self.game.difficulty]:
                self.game.drop()
                self.last_fall = now
            if now - self.last_process >= .018:
                self.game.tick()
                self.last_process = now
        width, height = self.swapchain_extent
        if self.intro_active:
            self.draw_text("Press Enter", 24, 54, self.font, mx.Color(218, 229, 232, 255))
        elif self.game.game_over:
            text = f"Game Over: Lines cleared: {self.game.lines} [Press Enter to Restart]"
            tw, th = self.text_size(text, self.font) or (400, 24)
            self.draw_text(text, max(0, (width - tw) // 2), max(0, (height - th) // 2), self.font)
        else:
            self.draw_text(f"Level {self.game.level}   Lines {self.game.lines}   Difficulty {self.game.difficulty + 1}", 24, 22, self.font, mx.Color(255, 244, 223, 255))
            panel_size = self.preview_size(width, height)
            if panel_size >= 72:
                self.draw_text("Next", width - panel_size - 12, 60, self.font)

    @staticmethod
    def preview_size(width, height):
        return min(180, int(width * .22), int(height * .30))

    def sprite_rect(self, sprite, command_buffer, x, y, width, height):
        sprite.draw(int(x), int(y), width=int(width), height=int(height))
        self.render_sprite_now(sprite, command_buffer)
        sprite.native.clear_queue()

    def draw_preview(self, command_buffer, width, height):
        if not self.game_started or self.intro_active or self.game.game_over:
            return
        size = self.preview_size(width, height)
        if size < 72:
            return
        x, y = width - size - 24, 88
        for rect in ((x, y, size, 4), (x, y + size - 4, size, 4), (x, y, 4, size), (x + size - 4, y, 4, size)):
            self.sprite_rect(self.border, command_buffer, *rect)
        blocks = self.game.next_piece.blocks
        min_x, max_x = min(b.x for b in blocks), max(b.x for b in blocks)
        min_y, max_y = min(b.y for b in blocks), max(b.y for b in blocks)
        cells_wide, cells_high = max_x - min_x + 1, max_y - min_y + 1
        block_size = int(min(34, (size - 56) / max(cells_wide, cells_high)))
        origin_x, origin_y = x + (size - cells_wide * block_size) / 2, y + (size - cells_high * block_size) / 2
        for b in blocks:
            self.sprite_rect(self.preview_blocks[b.type - 2], command_buffer, origin_x + (b.x - min_x) * block_size, origin_y + (b.y - min_y) * block_size, block_size, block_size)

    def render_model(self, model, command_buffer, image_index, matrix, texture, effects, uniforms):
        uniforms.model = matrix.reshape(16).tolist()
        uniforms.effects = effects
        model.native.render_with_push_constants(command_buffer, image_index, texture, uniforms, False)

    def block_matrix(self, x, y, size=CUBE_SCALE):
        return translation((x - (BOARD_WIDTH - 1) * .5) * CUBE_SPACING, ((BOARD_HEIGHT - 1) * .5 - y) * CUBE_SPACING, 0) @ scale(size, size, size)

    def draw_cube(self, command_buffer, image_index, block_type, x, y, uniforms):
        wildcard = block_type in (CLEAR, MATCH)
        index = 9 if wildcard else block_type - 2
        tint = self.wildcard_color if wildcard else BLOCK_TINTS[index]
        self.render_model(self.cube, command_buffer, image_index, self.block_matrix(x, y), index, (*tint, 2 if wildcard else 1), uniforms)

    def on_record_custom_rendering(self, command_buffer, image_index):
        width, height = self.swapchain_extent
        if width <= 0 or height <= 0:
            return
        background = self.backgrounds[self.game.level - 1]
        background.shader_params(self.background_time, 0, 0, 1)
        self.sprite_rect(background, command_buffer, 0, 0, width, height)
        uniforms = mx.native.ModelUniforms()
        view = look_at((0, .12, self.camera_distance), (0, .05, 0)) @ rotation_x(math.radians(self.grid_pitch)) @ rotation_y(math.radians(self.grid_yaw))
        uniforms.view = view.reshape(16).tolist()
        uniforms.projection = perspective(width / height).reshape(16).tolist()
        matrix = translation(0, 0, -CUBE_SCALE * .68) @ scale((BOARD_WIDTH + .35) * CUBE_SPACING, (BOARD_HEIGHT + .35) * CUBE_SPACING, CUBE_SCALE * .10)
        self.render_model(self.backdrop, command_buffer, image_index, matrix, 0, (.16, .16, .17, .62), uniforms)
        frame = [(-1, y) for y in range(BOARD_HEIGHT)] + [(BOARD_WIDTH, y) for y in range(BOARD_HEIGHT)] + [(x, BOARD_HEIGHT) for x in range(-1, BOARD_WIDTH + 1)]
        for x, y in frame:
            self.render_model(self.cube, command_buffer, image_index, self.block_matrix(x, y, CUBE_SCALE * .82), 10, (.58, .62, .66, 1), uniforms)
        for y, row in enumerate(self.game.board):
            for x, cell in enumerate(row):
                if cell.type == NULL or (cell.type == CLEAR and (cell.flash_counter // 6) % 2):
                    continue
                self.draw_cube(command_buffer, image_index, cell.type, x, y, uniforms)
        if (self.game_started or self.intro_active) and not self.game.game_over:
            for block in self.game.piece.blocks:
                self.draw_cube(command_buffer, image_index, block.type, block.x, block.y, uniforms)
        self.draw_preview(command_buffer, width, height)
        if self.intro_active:
            self.intro.shader_params(time.monotonic() - self.intro_started, 0, 0, max(0, self.intro_fade))
            self.sprite_rect(self.intro, command_buffer, 0, 0, width, height)
            self.sprite_rect(self.rain.sprite, command_buffer, 0, 0, width, height)


def main():
    parser = argparse.ArgumentParser(description="3D Puzzle Drop for mxvk_wrap")
    parser.add_argument("-r", "--resolution", default="1280x720", metavar="WIDTHxHEIGHT")
    parser.add_argument("-f", "--fullscreen", action="store_true")
    parser.add_argument("--no-vsync", action="store_true")
    parser.add_argument("--validation", action="store_true")
    args = parser.parse_args()
    try:
        width, height = map(int, args.resolution.lower().split("x"))
        if width <= 0 or height <= 0:
            raise ValueError
    except ValueError:
        parser.error("resolution must be positive WIDTHxHEIGHT, for example 1920x1080")
    PuzzleDropWindow(width, height, args.fullscreen, not args.no_vsync, args.validation).run()


if __name__ == "__main__":
    main()
