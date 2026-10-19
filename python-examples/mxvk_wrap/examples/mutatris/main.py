#!/usr/bin/env python3
"""The original four-sided Mutatris, translated to mxvk_wrap."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

EXAMPLES_DIR = Path(__file__).resolve().parents[3]
if str(EXAMPLES_DIR) not in sys.path:
    sys.path.insert(0, str(EXAMPLES_DIR))

import mxvk_wrap as mx

if __package__:
    from .compile_shaders import compile_shaders
    from .game import FOCUS_NEXT, PuzzleGame, cell_position
else:
    from compile_shaders import compile_shaders
    from game import FOCUS_NEXT, PuzzleGame, cell_position

KEY = {name: mx.native.key_code(name) for name in ("Return", "Keypad Enter", "Space", "Left", "Right", "Up", "Down", "W", "A", "S", "F3", "F8", "Backspace", "Escape")}
ARROWS = (KEY["Left"], KEY["Right"], KEY["Up"], KEY["Down"])
CONFIRM = (KEY["Return"], KEY["Keypad Enter"], KEY["Space"])
# Each row is local left, local right, cycle colors, inward drop.
MOVEMENT = (("Left", "Right", "Up", "Down"), ("Up", "Down", "Left", "Right"), ("Left", "Right", "Down", "Up"), ("Down", "Up", "Right", "Left"))
BLOCK_FILES = ("black", "clear", "dblue", "green", "ltblue", "orange", "red", "yellow")


class MutatrisWindow(mx.App):
    def __init__(self, width=1280, height=720, fullscreen=False, vsync=True, enable_crt=False, validation=False):
        compile_shaders()
        self.root = Path(__file__).resolve().parent
        self.data = self.root / "data"
        self.shaders = self.root / "shaders"
        super().__init__("Mutatris", width, height, fullscreen=fullscreen, vsync=vsync, validation=validation)
        self.screen, self.difficulty, self.focus = "startup", 0, 0
        self.game = None
        self.final_score = self.final_clears = 0
        self.crt_enabled = enable_crt
        self.fonts, self.font_scale = {}, None
        self.audio = self.controller = None
        self.music = self.line_sound = self.open_sound = -1
        self.background_index = self.shader_index = self.shader_level = -1
        self.effects = sorted((self.shaders / "effects").glob("*.spv"))
        self.startup_started = self.last_frame = self.last_drop = time.monotonic()
        self.last_keyboard = self.last_gamepad = 0
        self.controller_check = 0
        self.controller_buttons = set()
        self.console_input = ""
        self.console_history, self.history_index = [], 0
        self.console = None
        try:
            self.set_clear_color(0, 0, 0, 1)
            self.attach_post_processing_shader(str(self.shaders / "crt.frag.spv"), 0, 3, 0.5, 0.002)
            self.set_post_processing_shader_time_enabled(True)
            self.set_post_processing_enabled(enable_crt)
            vertex = str(self.shaders / "background.vert.spv")
            self.backgrounds = [mx.Sprite(self, self.data / f"blocks{index or ''}.png", vertex_shader=vertex, fragment_shader=str(self.shaders / "background.frag.spv")) for index in range(11)]
            self.pictures = {}
            for name in ("intro", "start", "lostlogo", "jblogo", "gameover", "highscore"):
                fading = name in ("intro", "lostlogo", "jblogo")
                self.pictures[name] = mx.Sprite(self, self.data / f"{name}.png", vertex_shader=vertex if fading else "", fragment_shader=str(self.shaders / "fade.frag.spv") if fading else "")
            self.blocks = [mx.Sprite(self, self.data / f"block_{name}.png") for name in BLOCK_FILES]
            # Console.show enables SDL text input; Python handles the commands
            # because the native console's event and callback APIs aren't bound.
            self.console = mx.native.Console()
            self.console.attach(self, str(self.data / "font.ttf"), 20)
            self.console.set_sprite_y_origin_top_left(True)
            self.console.set_prompt("")
            self.console.print_line("Press F3 to open/close. Type 'help' for commands.")
            if mx.native.has_mixer:
                self.audio = self.add(mx.Sound())
                self.music = self.audio.load_music(self.data / "music.ogg")
                self.line_sound = self.audio.load_wav(self.data / "line.wav")
                self.open_sound = self.audio.load_wav(self.data / "open.wav")
                self.play_sound(self.open_sound)
            self.sync_controller()
        except Exception:
            self.close()
            raise

    def close(self):
        if getattr(self, "_closed", True):
            return
        self.wait_idle()
        if self.console is not None:
            self.console.hide()
            self.console = None
        for font in self.fonts.values():
            font.close()
        self.fonts.clear()
        self.controller = None
        super().close()

    def play_sound(self, sound):
        if self.audio is not None and sound >= 0:
            self.audio.play(sound)

    def start_game(self):
        self.game = PuzzleGame(self.difficulty, lambda: self.play_sound(self.line_sound))
        self.focus, self.screen = 0, "playing"
        self.last_drop = self.last_frame = time.monotonic()
        self.last_keyboard = 0
        self.shader_level = self.background_index = self.shader_index = -1

    def game_over(self):
        self.final_score, self.final_clears = self.game.score, self.game.clears
        self.screen = "gameover"

    def confirm(self):
        if self.screen == "startup":
            self.screen = "difficulty" if time.monotonic() - self.startup_started >= 10.98 else "title"
        elif self.screen == "title":
            self.screen = "difficulty"
        elif self.screen == "difficulty":
            self.start_game()
        elif self.screen == "gameover":
            self.screen = "title"

    def drop_piece(self, hard=False):
        if self.screen != "playing":
            return
        piece = self.game.grids[self.focus].piece
        locked = piece.drop() if hard else piece.move_down()
        if locked:
            self.game.process()
            self.focus = FOCUS_NEXT[self.focus]
            self.last_drop = time.monotonic()
        elif not piece.check_location(piece.x, piece.y):
            self.game_over()

    def directional_key(self, key, gamepad=False):
        if self.screen != "playing":
            return
        piece = self.game.grids[self.focus].piece
        left, right, cycle, inward = (KEY[name] for name in MOVEMENT[self.focus])
        if key == left:
            piece.move(-1)
        elif key == right:
            piece.move(1)
        elif key == cycle and not gamepad:
            piece.shift_colors()
        elif key == inward:
            self.drop_piece()

    def run_command(self, text):
        args = text.split()
        if not args:
            return
        self.console.print_line("mutatris> " + text)
        self.console_history.append(text)
        self.history_index = len(self.console_history)
        command = args[0]
        if command == "help":
            self.console.print_line("help | clear | echo <text> | switch_shader | about | quit | exit")
        elif command == "clear":
            self.console.clear()
        elif command == "echo":
            self.console.print_line(" ".join(args[1:]))
        elif command == "switch_shader":
            self.console.print_line(self.switch_background())
        elif command == "about":
            self.console.print_line("Mutatris: rotating-grid puzzle game built with mxvk_wrap.")
        elif command in ("quit", "exit"):
            self.quit()
        else:
            self.console.print_line(f"Unknown command: {command}")

    def on_event(self, event):
        if event.key_down and event.key == KEY["F3"]:
            if not event.repeat:
                self.console.toggle()
            return
        if self.console.visible():
            if event.text:
                self.console_input += event.text
            elif event.key_down:
                if event.key == KEY["Backspace"]:
                    self.console_input = self.console_input[:-1]
                elif event.key in CONFIRM[:2]:
                    self.run_command(self.console_input)
                    self.console_input = ""
                elif event.key in (KEY["Up"], KEY["Down"]):
                    self.history_index = max(0, min(len(self.console_history), self.history_index + (-1 if event.key == KEY["Up"] else 1)))
                    self.console_input = self.console_history[self.history_index] if self.history_index < len(self.console_history) else ""
            return
        if event.mouse_button_down or event.type == 0x700:  # SDL_EVENT_FINGER_DOWN
            self.confirm()
        if not event.key_down or event.repeat:
            return
        key = event.key
        if key == KEY["Escape"]:
            if self.screen == "playing":
                self.game, self.screen = None, "difficulty"
            else:
                self.quit()
        elif key == KEY["F8"]:
            self.crt_enabled = not self.crt_enabled
            self.set_post_processing_enabled(self.crt_enabled)
        elif self.screen == "startup":
            if key in CONFIRM:
                self.confirm()
            else:
                self.screen = "title"
        elif self.screen == "difficulty" and key in (KEY["Left"], KEY["Right"]):
            self.difficulty = max(0, min(2, self.difficulty + (-1 if key == KEY["Left"] else 1)))
        elif self.screen != "playing" and key in CONFIRM:
            self.confirm()
        elif self.screen == "playing":
            piece = self.game.grids[self.focus].piece
            if key == KEY["W"]:
                piece.shift_colors()
            elif key in (KEY["A"], KEY["Space"]):
                piece.rotate()
            elif key == KEY["S"]:
                self.drop_piece(hard=True)

    def sync_controller(self):
        if self.controller is not None and not self.controller.native.active():
            self.controller.close()
            self.controller = None
            self.controller_buttons.clear()
        if self.controller is None and mx.Controller.count() > 0:
            controller = mx.Controller()
            if controller.native.active():
                self.controller = self.add(controller)
            else:
                controller.close()

    def poll_input(self, now):
        if now - self.controller_check > 1:
            self.controller_check = now
            self.sync_controller()
        if self.controller is not None:
            buttons = {button for button in (0, 1, 3, 4, 6, 11, 12, 13, 14) if self.controller.button(button)}
            pressed = buttons - self.controller_buttons
            self.controller_buttons = buttons
            if not self.console.visible():
                for button in sorted(pressed):
                    if button == 4:  # SDL_GAMEPAD_BUTTON_BACK
                        self.quit()
                    elif self.screen != "playing":
                        if button in (0, 6):
                            self.confirm()
                        elif self.screen == "difficulty" and button in (13, 14):
                            self.difficulty = max(0, min(2, self.difficulty + (-1 if button == 13 else 1)))
                    elif button == 0:
                        self.game.grids[self.focus].piece.shift_colors()
                    elif button == 1:
                        self.game.grids[self.focus].piece.rotate()
                    elif button == 3:
                        self.drop_piece(hard=True)
                    elif button in (11, 12, 13, 14) and now - self.last_gamepad >= 0.12:
                        self.last_gamepad = now
                        self.directional_key(KEY[{11: "Up", 12: "Down", 13: "Left", 14: "Right"}[button]], gamepad=True)
        if self.screen == "playing":
            # Match SDL keyboard polling and the shared 120 ms repeat timer.
            key = next((key for key in ARROWS if mx.key_pressed(key)), None)
            if key is None:
                self.last_keyboard = 0
            elif now - self.last_keyboard >= 0.12:
                self.last_keyboard = now
                self.directional_key(key)

    def switch_background(self):
        import random
        self.background_index = random.choice([i for i in range(11) if i != self.background_index])
        if self.effects:
            options = [i for i in range(len(self.effects)) if i != self.shader_index] or [0]
            self.shader_index = random.choice(options)
            self.backgrounds[self.background_index].native.set_fragment_shader_path(str(self.effects[self.shader_index]))
        self.shader_level = self.game.level if self.game else 0
        return f"blocks{self.background_index or ''}.png / {self.effects[self.shader_index].name if self.effects else 'background'}"

    def sx(self, value):
        return int(value * self.swapchain_extent[0] / 1280)

    def sy(self, value):
        return int(value * self.swapchain_extent[1] / 720)

    def ensure_fonts(self):
        width, height = self.swapchain_extent
        scale = max(0.2, min(width / 1280, height / 720))
        sizes = (24, 44, 220, 36, 18, 30, 20)
        scaled = tuple(max(1, int(size * scale + 0.5)) for size in sizes)
        if scaled != self.font_scale:
            self.wait_idle()
            for font in self.fonts.values():
                font.close()
            self.fonts = {size: mx.Font(self.data / "font.ttf", pixels) for size, pixels in zip(sizes, scaled)}
            self.font_scale = scaled

    def text(self, text, x, y, size=24, color=None):
        self.draw_text(text, self.sx(x), self.sy(y), self.fonts[size], color or mx.Color(255, 255, 255))

    def picture(self, name, alpha=1):
        sprite = self.pictures[name]
        sprite.shader_params(alpha)
        width, height = self.swapchain_extent
        sprite.draw(0, 0, width=width, height=height)

    def draw_title(self, alpha=1):
        self.picture("intro", alpha)
        white = mx.Color(255, 255, 255, int(255 * alpha))
        self.text("[Press Enter to Play]", 25, 25, 44, white)
        self.text("Mutatris", 50, 250, 220, white)
        self.text("lostsidedead.biz", 780, 670, 44, mx.Color(80, 120, 255, int(255 * alpha)))

    def draw_startup(self, elapsed):
        fade = 2.52
        if elapsed < fade:
            self.picture("lostlogo", elapsed / fade)
        elif elapsed < fade * 2:
            self.picture("lostlogo", 2 - elapsed / fade)
        elif elapsed < fade * 3:
            self.picture("jblogo", elapsed / fade - 2)
        elif elapsed < fade * 3 + 0.9:
            self.picture("jblogo")
        elif elapsed < fade * 4 + 0.9:
            transition = (elapsed - (fade * 3 + 0.9)) / fade
            self.draw_title(max(0, min(1, (transition - 0.78) / 0.22)))
            self.picture("jblogo", 1 - transition)
        elif elapsed < fade * 5 + 0.9:
            self.draw_title()
        else:
            self.screen = "title"
            self.draw_title()

    def draw_difficulty(self):
        self.picture("start")
        self.text("Mutatris", 560, 325, 36)
        text_width, text_height = self.text_size("Press Space", self.fonts[18])
        self.draw_text("Press Space", self.sx(530) + (self.sx(217) - text_width) // 2, self.sy(548) + (self.sy(78) - text_height) // 2, self.fonts[18])
        for index, label in enumerate(("Easy", "Medium", "Hard")):
            self.text(label, 430 + index * 180, 475, 30, mx.Color(255, 230, 64) if index == self.difficulty else None)

    def draw_frame(self, index):
        thickness = 4 if index == self.focus else 2
        if index == 0:
            rects = ((448 - thickness, -thickness, 384 + thickness * 2, thickness), (448 - thickness, 0, thickness, 160), (832, 0, thickness, 160))
        elif index == 1:
            rects = ((-thickness, 160 - thickness, 448 + thickness, thickness), (-thickness, 560, 448 + thickness, thickness), (-thickness, 160, thickness, 400))
        elif index == 2:
            rects = ((448 - thickness, 720, 384 + thickness * 2, thickness), (448 - thickness, 560, thickness, 160), (832, 560, thickness, 160))
        else:
            rects = ((832, 160 - thickness, 448 + thickness, thickness), (832, 560, 448 + thickness, thickness), (1280, 160, thickness, 400))
        for x, y, w, h in rects:
            self.blocks[0].draw(self.sx(x), self.sy(y), width=max(1, self.sx(w)), height=max(1, self.sy(h)))

    def draw_cell(self, index, x, y, color):
        if x < 0 or y < 0:
            return
        px, py = cell_position(index, x, y)
        sprite = self.blocks[1 + abs(color) % 6 if color < 0 else max(0, min(7, color))]
        width, height = max(1, self.sx(14)), max(1, self.sy(14))
        if color < 0:
            sprite.native.draw_rotated(self.sx(px), self.sy(py), width / sprite.native.width, height / sprite.native.height, min(23, abs(color) - 1) * 15.0)
        else:
            sprite.draw(self.sx(px), self.sy(py), width=width, height=height)

    def draw_playing(self, now, elapsed):
        self.game.process()
        if self.shader_level != self.game.level:
            self.switch_background()
        background = self.backgrounds[self.background_index]
        background.shader_params(now - self.startup_started)
        width, height = self.swapchain_extent
        background.draw(0, 0, width=width, height=height)
        self.game.animate_clears(elapsed * 1000)
        for index, grid in enumerate(self.game.grids):
            self.draw_frame(index)
            for y in range(grid.height):
                for x in range(grid.width):
                    self.draw_cell(index, x, y, grid.at(x, y).color)
            if index == self.focus:
                for (x, y), color in zip(grid.piece.cells(), grid.piece.colors):
                    self.draw_cell(index, x, y, color)
        if not self.console.visible():
            self.text(f"Level: {self.game.level + 1}  Timeout: {self.game.timeout}", 25, 25)
            self.text(f"Score: {self.game.score}", 25, 55)
            self.text(f"Direction: {self.focus}", 25, 85)
            self.text("Arrows move  W shift  A rotate  S drop", 25, 670, color=mx.Color(220, 220, 220))
        if (now - self.last_drop) * 1000 >= self.game.timeout:
            piece = self.game.grids[self.focus].piece
            if piece.direction == 3 or piece.check_location(piece.x, piece.y) or piece.y != 0:
                self.drop_piece()
                self.last_drop = now
            else:
                self.game_over()

    def draw(self):
        now = time.monotonic()
        elapsed, self.last_frame = max(0, now - self.last_frame), now
        if min(self.swapchain_extent) <= 0:
            return
        self.ensure_fonts()
        if self.audio is not None and not self.audio.music_playing(self.music):
            self.audio.play_music(self.music, -1)
        self.poll_input(now)
        if self.screen == "startup":
            self.draw_startup(now - self.startup_started)
        elif self.screen == "title":
            self.draw_title()
        elif self.screen == "difficulty":
            self.draw_difficulty()
        elif self.screen == "playing":
            self.draw_playing(now, elapsed)
        elif self.screen == "gameover":
            high = self.final_score >= 200
            self.picture("highscore" if high else "gameover")
            self.text(f"{'High Score' if high else 'Game Over Score'}: {self.final_score}  Clears: {self.final_clears}", 25, 25)
            self.text("[ Press Space ]", 25, 400 if high else 100)
        if self.console.visible():
            self.console.set_prompt("mutatris> " + self.console_input)
        self.console.draw()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-r", "--resolution", default="1280x720", metavar="WIDTHxHEIGHT")
    parser.add_argument("-f", "--fullscreen", action="store_true")
    parser.add_argument("--no-vsync", action="store_true")
    parser.add_argument("--enable-crt", action="store_true")
    parser.add_argument("--validation", action="store_true")
    args = parser.parse_args()
    try:
        width, height = map(int, args.resolution.lower().split("x"))
        if min(width, height) <= 0:
            raise ValueError
    except ValueError:
        parser.error("resolution must be positive WIDTHxHEIGHT, for example 1280x720")
    MutatrisWindow(width, height, args.fullscreen, not args.no_vsync, args.enable_crt, args.validation).run()


if __name__ == "__main__":
    main()
