#!/usr/bin/env python3
"""MXVK 3D Tetris: a Python wrapper port of examples/tetris."""

from __future__ import annotations

import argparse
import math
from pathlib import Path
import sys
import time

PYTHON_EXAMPLES = Path(__file__).resolve().parents[3]
if str(PYTHON_EXAMPLES) not in sys.path:
    sys.path.insert(0, str(PYTHON_EXAMPLES))

import mxvk_wrap as mx

if __package__ in (None, ''):
    from mxvk_wrap.examples.tetris3d.compile_shaders import compile_shaders
    from mxvk_wrap.examples.tetris3d.game import Game, HEIGHT, PIECES, WIDTH
    from mxvk_wrap.examples.tetris3d.matrices import look_at, perspective, rotation_x, rotation_y, rotation_z, scale, translation
    from mxvk_wrap.examples.tetris3d.network import Network, PORT
    from mxvk_wrap.examples.tetris3d.scores import Scores
else:
    from .compile_shaders import compile_shaders
    from .game import Game, HEIGHT, PIECES, WIDTH
    from .matrices import look_at, perspective, rotation_x, rotation_y, rotation_z, scale, translation
    from .network import Network, PORT
    from .scores import Scores

DATA = Path(__file__).resolve().parent / 'data'
MANIFESTS = ('cyan', 'blue', 'orange', 'yellow', 'green', 'purple', 'red', 'gray')
BLOCKS = ('ltblue', 'dblue', 'orange', 'yellow', 'green', 'purple', 'red', 'gray')
TINTS = ((0.65, 0.95, 1.0), (0.45, 0.56, 1.0), (1.0, 0.63, 0.25), (1.0, 0.92, 0.35), (0.48, 1.0, 0.45), (0.82, 0.48, 1.0), (1.0, 0.42, 0.42), (0.55, 0.58, 0.62))
KEYS = {name: mx.native.key_code(name) for name in ('Return', 'Backspace', 'PageUp', 'PageDown', 'W', 'S', 'A', 'D', 'Q', 'E', 'Z', 'H', 'J')}
NAME_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ '
MENU = ('New Game', 'Network Multiplayer', 'High Scores', 'Credits')


class Tetris3D(mx.App):
    def __init__(self, width=1280, height=720, fullscreen=False, validation=False, mute=False, scores=None, start=False):
        compile_shaders()
        self.models = []
        super().__init__('MXVK Wrap 3D Tetris', width, height, fullscreen=fullscreen, validation=validation, vsync=True)
        self.game = Game()
        self.network = self.add(Network())
        self.scores = Scores(scores or Path(__file__).resolve().parent / 'scores.json')
        self.screen = 'game' if start else 'intro'
        self.previous_screen = None
        self.screen_started = self.started = self.last_frame = time.monotonic()
        self.level_started = -10.0
        self.pitch = self.yaw = self.roll = 0.0
        self.distance = 2.8
        self.cursor = 0
        self.name = ''
        self.name_index = 0
        self.entering_name = False
        self.address = '127.0.0.1'
        self.result = ''
        self.held = set()
        self.repeat_timers = {}
        self.pad_buttons = set()
        self.controller = None
        self.controller_check = 0.0
        self.font = None
        self.audio = None
        self.track = -1
        self.frame_limit = 0
        self.frame_count = 0
        self.jobs = []
        self.pool = [[] for _ in MANIFESTS]
        try:
            self.font = self.add(mx.Font(DATA / 'font.ttf', 22))
            self.backgrounds = {screen: self.sprite(filename, 'screen_fade.frag') for screen, filename in (('menu', 'start_screen.png'), ('scores', 'high_scores_screen.png'), ('credits', 'credits_screen.png'), ('network', 'multiplayer_screen.png'))}
            self.background = self.sprite('psychedelic_background.png', 'background_transition.frag')
            self.intro = self.sprite('intro.png', 'intro.frag')
            self.gameover = self.sprite('gameover.png', 'screen_fade.frag')
            self.previews = [mx.Sprite(self, DATA / f'block_{color}.png') for color in BLOCKS]
            self.frame = [(self.load_cube(7), x, y) for y in range(HEIGHT) for x in (-1, WIDTH)]
            self.frame += [(self.load_cube(7), x, -1) for x in range(-1, WIDTH + 1)]
            self.tux = mx.Model(self, DATA / 'tux/tux.obj', textures=DATA / 'tux/tux.mtl', texture_directory=DATA / 'tux', scale=0.35)
            self.tux.shaders(self, DATA / 'model.vert.spv', DATA / 'model.frag.spv')
            self.models.append(self.tux)
            if not mute and mx.native.has_mixer:
                self.audio = self.add(mx.Sound())
                self.track = self.audio.load_music(DATA / 'music.ogg')
                self.audio.play_music(self.track, -1)
        except Exception:
            self.close()
            raise

    def sprite(self, filename, fragment):
        return mx.Sprite(self, DATA / filename, vertex_shader=str(DATA / 'sprite.vert.spv'), fragment_shader=str(DATA / (fragment + '.spv')))

    def load_cube(self, color):
        model = mx.Model(self, DATA / 'cube.mxmod.z', textures=DATA / f'manifest_{MANIFESTS[color]}.txt', texture_directory=DATA)
        model.shaders(self, DATA / 'tetris_piece.vert.spv', DATA / 'tetris_piece.frag.spv')
        self.models.append(model)
        return model

    def on_swapchain_recreated(self):
        for model in self.models:
            if model.native is not None:
                model.native.resize(self)

    def change_screen(self, screen):
        if screen != self.screen:
            self.previous_screen = self.screen
            self.screen = screen
            self.screen_started = time.monotonic()
            self.held.clear()
            self.repeat_timers.clear()

    def start_game(self):
        self.game.reset()
        self.result = ''
        self.entering_name = False
        self.change_screen('game')

    def menu(self):
        self.network.close()
        self.change_screen('menu')

    def action(self, key, repeat=False):
        if key == mx.KEY_ESCAPE:
            if self.screen == 'menu':
                self.quit()
            else:
                self.menu()
            return
        if self.screen == 'intro':
            if key in (KEYS['Return'], mx.KEY_SPACE):
                self.change_screen('menu')
        elif self.screen == 'menu':
            if key in (mx.KEY_UP, mx.KEY_DOWN):
                self.cursor = (self.cursor + (1 if key == mx.KEY_DOWN else -1)) % len(MENU)
            elif key == KEYS['Return']:
                if self.cursor == 0:
                    self.start_game()
                else:
                    self.change_screen(('game', 'network', 'scores', 'credits')[self.cursor])
        elif self.screen in ('scores', 'credits'):
            if key == KEYS['Return']:
                self.menu()
        elif self.screen == 'network':
            if key == KEYS['H']:
                self.network.host()
            elif key in (KEYS['J'], KEYS['Return']):
                self.network.join(self.address)
            elif key == KEYS['Backspace']:
                self.address = self.address[:-1]
            elif len(self.address) < 15 and key in map(ord, '0123456789.'):
                self.address += chr(key)
        elif self.screen == 'over':
            if self.entering_name:
                if key in (mx.KEY_UP, mx.KEY_DOWN):
                    self.name_index = (self.name_index + (1 if key == mx.KEY_UP else -1)) % len(NAME_CHARS)
                elif key == mx.KEY_SPACE and len(self.name) < 16:
                    self.name += NAME_CHARS[self.name_index]
                elif key == KEYS['Backspace']:
                    self.name = self.name[:-1]
                elif key == KEYS['Return']:
                    if self.scores.add(self.name, self.game.score):
                        self.entering_name = False
                        self.change_screen('scores')
            elif key in (KEYS['Return'], mx.KEY_R):
                self.network.close()
                self.start_game()
            elif key == KEYS['H']:
                self.change_screen('scores')
        elif self.screen == 'game':
            if key == mx.KEY_LEFT:
                self.game.move(-1)
            elif key == mx.KEY_RIGHT:
                self.game.move(1)
            elif key == mx.KEY_DOWN:
                self.game.soft_drop()
            elif key == mx.KEY_UP and not repeat:
                self.game.rotate()
            elif key in (KEYS['Z'], mx.KEY_SPACE) and not repeat:
                self.game.hard_drop()
            elif key == mx.KEY_R and not repeat:
                self.network.close()
                self.game.reset()
                self.change_screen('intro')

    def on_event(self, event):
        if event.key_up:
            self.held.discard(event.key)
            self.repeat_timers.pop(event.key, None)
        elif event.key_down and not event.repeat:
            key = event.key
            self.held.add(key)
            self.repeat_timers[key] = 0.22
            self.action(key)

    def update_controller(self, delta):
        now = time.monotonic()
        if self.controller is None and now - self.controller_check > 2:
            self.controller_check = now
            if mx.Controller.count():
                try:
                    self.controller = self.add(mx.Controller())
                except RuntimeError:
                    return
        if self.controller is None:
            return
        pad = self.controller
        if not pad.native.active():
            pad.close()
            self.controller = None
            self.pad_buttons.clear()
            return
        buttons = {i for i in (0, 1, 4, 9, 10, 11, 12, 13, 14) if pad.button(i)}
        mapping = {0: mx.KEY_UP if self.screen == 'game' else KEYS['Return'], 1: KEYS['Z'] if self.screen == 'game' else mx.KEY_ESCAPE, 4: mx.KEY_ESCAPE, 11: mx.KEY_UP, 12: mx.KEY_DOWN, 13: mx.KEY_LEFT, 14: mx.KEY_RIGHT}
        if self.screen == 'game':
            for button in (12, 13, 14):
                mapping.pop(button)
        if self.screen == 'over' and self.entering_name:
            mapping[0], mapping[1], mapping[2] = mx.KEY_SPACE, KEYS['Backspace'], KEYS['Return']
            if pad.button(2):
                buttons.add(2)
        for button in buttons - self.pad_buttons:
            if button in mapping:
                self.action(mapping[button])
        self.pad_buttons = buttons
        if self.screen != 'game':
            return
        self.yaw += pad.axis(2) / 32768 * 120 * delta if abs(pad.axis(2)) > 10000 else 0
        self.pitch += pad.axis(3) / 32768 * 100 * delta if abs(pad.axis(3)) > 10000 else 0
        self.distance = max(1.4, min(5.5, self.distance + (int(9 in buttons) - int(10 in buttons)) * delta))
        for name, active, key in (('padleft', pad.axis(0) < -10000 or 13 in buttons, mx.KEY_LEFT), ('padright', pad.axis(0) > 10000 or 14 in buttons, mx.KEY_RIGHT), ('paddown', pad.axis(1) > 10000 or 12 in buttons, mx.KEY_DOWN)):
            if active:
                timer = self.repeat_timers.get(name, 0) - delta
                if timer <= 0:
                    self.action(key, repeat=True)
                    timer = 0.12 if name != 'paddown' else 0.08
                self.repeat_timers[name] = timer
            else:
                self.repeat_timers.pop(name, None)

    def update(self, delta):
        if self.screen == 'intro' and time.monotonic() - self.screen_started >= 6:
            self.change_screen('menu')
        was_connected = self.network.connected
        if self.network.update(self.game):
            self.start_game()
        elif was_connected and not self.network.connected and self.screen in ('game', 'over'):
            self.result = self.network.status
            self.change_screen('over')
        self.update_controller(delta)
        for key in tuple(self.held):
            if key not in (mx.KEY_LEFT, mx.KEY_RIGHT, mx.KEY_DOWN):
                continue
            self.repeat_timers[key] = self.repeat_timers.get(key, 0.22) - delta
            if self.repeat_timers[key] <= 0:
                self.action(key, repeat=True)
                self.repeat_timers[key] += 0.08 if key == mx.KEY_DOWN else 0.12
        if self.screen != 'game':
            return
        for negative, positive, attribute in (('W', 'S', 'pitch'), ('A', 'D', 'yaw'), ('Q', 'E', 'roll')):
            value = (int(KEYS[positive] in self.held) - int(KEYS[negative] in self.held)) * delta * 90
            setattr(self, attribute, (getattr(self, attribute) + value) % 360)
        self.distance = max(1.4, min(5.5, self.distance + (int(KEYS['PageDown'] in self.held) - int(KEYS['PageUp'] in self.held)) * delta))
        level = self.game.level
        self.game.update(delta)
        if self.game.level != level:
            self.level_started = time.monotonic()
        opponent = self.network.opponent
        if self.game.game_over or (opponent and opponent['over']):
            self.result = ('You win!' if not self.game.game_over else 'Game over') if self.network.connected else ''
            self.entering_name = not self.network.connected and self.scores.qualifies(self.game.score)
            self.name = ''
            self.change_screen('over')

    def text(self, text, x, y, color=(255, 255, 255), centered=False):
        if centered:
            size = self.text_size(text, self.font)
            x -= (size[0] if size else len(text) * 12) // 2
        self.draw_text(text, int(x), int(y), self.font, mx.Color(*color, 255))

    def draw(self):
        now = time.monotonic()
        delta = min(0.25, max(0, now - self.last_frame))
        self.last_frame = now
        self.update(delta)
        width, height = self.swapchain_extent
        self.jobs = []
        if self.screen in ('game', 'over'):
            counts = [0] * 8
            for x, y, color in self.game.visible_blocks():
                index = counts[color]
                if index == len(self.pool[color]):
                    self.pool[color].append(self.load_cube(color))
                self.jobs.append((self.pool[color][index], x, y, color))
                counts[color] += 1
            self.text(f'Score: {self.game.score}', 15, 15)
            self.text(f'Lines Cleared: {self.game.lines}', 15, 45, (255, 220, 120))
            self.text(f'Level: {self.game.level}', 15, 75, (120, 220, 255))
            self.text('Next', 24, 122)
            cell = max(10, min(28, width // 40))
            for dx, dy in PIECES[self.game.next_color]:
                self.previews[self.game.next_color].draw(24 + (dx + 1) * cell, 160 + (1 - dy) * cell, width=cell, height=cell)
            self.text('Arrows move/rotate | Z drop | R intro', 15, height - 58, (210, 220, 240))
            self.text('WASD tilt | Q/E roll | PgUp/PgDn zoom | Esc menu', 15, height - 30, (210, 220, 240))
            self.draw_opponent(width, height)
        cx = width // 2
        if self.screen == 'menu':
            self.text('MXVK 3D Tetris', cx, height * 0.18, (255, 255, 0), True)
            for i, item in enumerate(MENU):
                self.text(('> ' if i == self.cursor else '') + item, cx, height * (0.40 + i * 0.07), (255, 255, 0) if i == self.cursor else (255, 255, 255), True)
            self.text('Up/Down choose | Enter select | Escape quit', cx, height * 0.87, centered=True)
        elif self.screen == 'intro':
            self.text('Enter / Space to continue', cx, height * 0.9, centered=True)
        elif self.screen == 'scores':
            self.text('High Scores', cx, height * 0.12, (255, 255, 0), True)
            for i in range(10):
                name, score = self.scores.entries[i] if i < len(self.scores.entries) else ('---', '---')
                self.text(f'{i + 1:2}. {name}', width * 0.3, height * 0.23 + i * 30)
                self.text(str(score), width * 0.65, height * 0.23 + i * 30)
            self.text('Enter / Escape for menu', cx, height * 0.87, centered=True)
        elif self.screen == 'credits':
            self.text('MXVK 3D Tetris', cx, height * 0.18, (255, 255, 0), True)
            self.text('Built with Vulkan, SDL3 and mxvk_wrap', cx, height * 0.25, centered=True)
            self.text('Python port of the native MXVK Tetris example', cx, height * 0.31, centered=True)
            self.text('Enter / Escape for menu', cx, height * 0.87, centered=True)
        elif self.screen == 'network':
            for i, text in enumerate(('Network Multiplayer', f'TCP port: {PORT}', f'Peer IP: {self.address}_', self.network.status, 'H host | J/Enter join | Backspace edit', 'Escape for menu')):
                self.text(text, cx, height * 0.18 + i * 48, centered=True)
        elif self.screen == 'over':
            lines = ['Game Over', f'Final Score: {self.game.score}']
            if self.entering_name:
                lines += ['New high score', f'Name: {self.name}_   Pick: [{NAME_CHARS[self.name_index]}]', 'Up/Down choose | Space add | Backspace delete', 'Enter save | Escape skip']
            else:
                lines += [self.result, 'Enter restart | H high scores | Escape menu']
            if self.scores.error:
                lines.append(self.scores.error)
            for i, text in enumerate(lines):
                self.text(text, cx, height * 0.25 + i * 38, (255, 255, 0) if i == 0 else (255, 255, 255), True)
        self.frame_count += 1
        if self.frame_limit and self.frame_count >= self.frame_limit:
            self.quit()

    def draw_opponent(self, width, height):
        opponent = self.network.opponent
        if opponent is None:
            return
        cell = max(4, min(16, width // 55, (height - 230) // HEIGHT))
        left, top = width - WIDTH * cell - 20, 100
        self.text('Opponent', left, top - 30)
        cells = [(i % WIDTH, i // WIDTH, color) for i, color in enumerate(opponent['board']) if color >= 0]
        if not opponent['over']:
            cells += [(opponent['x'] + dx, opponent['y'] + dy, opponent['color']) for dx, dy in opponent['cells']]
        for x, y, color in cells:
            if 0 <= x < WIDTH and 0 <= y < HEIGHT:
                self.previews[color].draw(left + x * cell, top + (HEIGHT - 1 - y) * cell, width=cell, height=cell)
        self.text(f"Score {opponent['score']}", left, top + HEIGHT * cell + 10)

    def render_model(self, model, matrix, view, projection, tint, command_buffer, image_index):
        uniforms = mx.native.ModelUniforms()
        uniforms.model = matrix.reshape(16).tolist()
        uniforms.view = view.reshape(16).tolist()
        uniforms.projection = projection.reshape(16).tolist()
        uniforms.effects = (*tint, 1.0)
        model.native.update_uniforms(image_index, uniforms)
        model.native.render(command_buffer, image_index, False)

    def render_background(self, screen, command_buffer, width, height, alpha=1.0):
        sprite = self.backgrounds.get(screen)
        if sprite is None:
            return
        sprite.shader_params(0, 0, 0, alpha)
        sprite.draw(0, 0, width=width, height=height)
        self.render_sprite_now(sprite, command_buffer)
        sprite.native.clear_queue()

    def on_record_custom_rendering(self, command_buffer, image_index):
        width, height = self.swapchain_extent
        if width <= 0 or height <= 0:
            return
        now = time.monotonic()
        fade = min(1.0, (now - self.screen_started) / 0.36)
        if self.screen in ('game', 'over'):
            elapsed = now - self.level_started
            self.background.shader_params(elapsed if elapsed < 1.25 else 0, 0, 0, 0)
            self.background.draw(0, 0, width=width, height=height)
            self.render_sprite_now(self.background, command_buffer)
            self.background.native.clear_queue()
            view = look_at((0.0, 0.1, self.distance), (0.0, 0.0, 0.0)) @ rotation_x(math.radians(self.pitch)) @ rotation_y(math.radians(self.yaw)) @ rotation_z(math.radians(self.roll))
            projection = perspective(width / height)
            for model, x, y, color in self.jobs:
                matrix = translation((x - 4.5) * 0.085, (y - 9.5) * 0.085, 0) @ scale(0.085, 0.085, 0.085)
                self.render_model(model, matrix, view, projection, TINTS[color], command_buffer, image_index)
            for model, x, y in self.frame:
                matrix = translation((x - 4.5) * 0.085, (y - 9.5) * 0.085, 0) @ scale(*([0.085 * 0.82] * 3))
                self.render_model(model, matrix, view, projection, TINTS[7], command_buffer, image_index)
            if self.screen == 'over':
                self.gameover.shader_params(0, 0, 0, min(0.85, fade))
                self.gameover.draw(0, 0, width=width, height=height)
                self.render_sprite_now(self.gameover, command_buffer)
                self.gameover.native.clear_queue()
        elif self.screen == 'intro':
            elapsed = now - self.screen_started
            self.intro.shader_params(elapsed, 0, 0, min(1, max(0, 6 - elapsed)))
            self.intro.draw(0, 0, width=width, height=height)
            self.render_sprite_now(self.intro, command_buffer)
            self.intro.native.clear_queue()
        else:
            self.render_background(self.previous_screen, command_buffer, width, height)
            self.render_background(self.screen, command_buffer, width, height, fade)
            if self.screen == 'credits':
                matrix = translation(0, -0.34, 0) @ rotation_y((now - self.started) * 0.75) @ scale(0.228, 0.228, 0.228)
                self.render_model(self.tux, matrix, look_at((0, 0.25, 2.8), (0, 0.05, 0)), perspective(width / height), (1, 1, 1), command_buffer, image_index)
        if self.screen == 'game' and fade < 1:
            self.render_background(self.previous_screen, command_buffer, width, height, 1 - fade)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('-r', '--resolution', default='1280x720', help='WIDTHxHEIGHT')
    parser.add_argument('-f', '--fullscreen', action='store_true')
    parser.add_argument('--validation', action='store_true')
    parser.add_argument('--mute', action='store_true')
    parser.add_argument('--scores', type=Path, help='high-score JSON file')
    parser.add_argument('--start', action='store_true', help='start gameplay immediately')
    parser.add_argument('--frames', type=int, default=0, help='exit after N frames (smoke testing)')
    args = parser.parse_args()
    try:
        width, height = map(int, args.resolution.lower().split('x'))
        if min(width, height) <= 0:
            raise ValueError
    except ValueError:
        parser.error('resolution must be positive WIDTHxHEIGHT')
    app = Tetris3D(width, height, args.fullscreen, args.validation, args.mute, args.scores, args.start)
    app.frame_limit = args.frames
    app.run()


if __name__ == '__main__':
    main()
