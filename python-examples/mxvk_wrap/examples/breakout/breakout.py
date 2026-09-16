#!/usr/bin/env python3
## @file breakout.py
## @brief A 3D Breakout game implemented with the @c mxvk_wrap façade.
## @details This is the wrapper version of the native Breakout sample. It keeps
## its MXMOD/OBJ artwork, animated background, collision rules, score, and
## three-life game loop while using @c App, @c Sprite, @c Model, and @c Font.

from __future__ import annotations

import math
from pathlib import Path
import random
import sys
import time

import numpy as np

EXAMPLES_DIR = Path(__file__).resolve().parents[3]
if str(EXAMPLES_DIR) not in sys.path:
    sys.path.insert(0, str(EXAMPLES_DIR))

import mxvk_wrap as mx

GAME_LEFT = -5.0
GAME_RIGHT = 5.0
GAME_TOP = 3.75
GAME_BOTTOM = -3.75
PADDLE_SPEED = 5.0
BALL_SPEED = 4.0
BALL_RADIUS = 0.25
DEFAULT_ZOOM = 0.8
MIN_ZOOM = 0.45
MAX_ZOOM = 1.6
ZOOM_SPEED = 0.8
ROTATION_SPEED = 50.0
MAX_MISSES = 3
BLOCK_ROWS = 5
BLOCK_COLUMNS = 11
KEY_A = mx.native.key_code("A")
KEY_D = mx.native.key_code("D")
KEY_H = mx.native.key_code("H")
KEY_L = mx.native.key_code("L")
KEY_Q = mx.native.key_code("Q")
KEY_S = mx.native.key_code("S")
KEY_W = mx.native.key_code("W")
KEY_PAGE_UP = mx.native.key_code("PageUp")
KEY_PAGE_DOWN = mx.native.key_code("PageDown")
KEY_1 = mx.native.key_code("1")
KEY_2 = mx.native.key_code("2")
KEY_3 = mx.native.key_code("3")


def translation(x: float, y: float, z: float) -> np.ndarray:
    matrix = np.eye(4, dtype=np.float32)
    matrix[:3, 3] = (x, y, z)
    return matrix


def scale(x: float, y: float, z: float) -> np.ndarray:
    return np.diag((x, y, z, 1.0)).astype(np.float32)


def rotation_x(angle: float) -> np.ndarray:
    cosine, sine = math.cos(angle), math.sin(angle)
    return np.array(((1.0, 0.0, 0.0, 0.0), (0.0, cosine, -sine, 0.0), (0.0, sine, cosine, 0.0), (0.0, 0.0, 0.0, 1.0)), dtype=np.float32)


def rotation_y(angle: float) -> np.ndarray:
    cosine, sine = math.cos(angle), math.sin(angle)
    return np.array(((cosine, 0.0, sine, 0.0), (0.0, 1.0, 0.0, 0.0), (-sine, 0.0, cosine, 0.0), (0.0, 0.0, 0.0, 1.0)), dtype=np.float32)


def perspective(fov: float, aspect: float, near: float, far: float) -> np.ndarray:
    tangent = math.tan(fov * 0.5)
    matrix = np.zeros((4, 4), dtype=np.float32)
    matrix[0, 0] = 1.0 / (aspect * tangent)
    matrix[1, 1] = -1.0 / tangent
    matrix[2, 2] = far / (near - far)
    matrix[2, 3] = far * near / (near - far)
    matrix[3, 2] = -1.0
    return matrix


def ortho(left: float, right: float, bottom: float, top: float, near: float = -100.0, far: float = 100.0) -> np.ndarray:
    ## @brief Build the right-handed Vulkan zero-to-one orthographic matrix used by GLM.
    matrix = np.eye(4, dtype=np.float32)
    matrix[0, 0] = 2.0 / (right - left)
    matrix[1, 1] = -2.0 / (top - bottom)
    matrix[2, 2] = -1.0 / (far - near)
    matrix[0, 3] = -(right + left) / (right - left)
    matrix[1, 3] = (top + bottom) / (top - bottom)
    matrix[2, 3] = -near / (far - near)
    return matrix


class BreakoutGame(mx.App):
    ## @brief Playable 3D Breakout window with MXVK model rendering.

    def __init__(self) -> None:
        self.asset_dir = Path(__file__).resolve().parent / "data"
        super().__init__("MXVK Wrap 3D Breakout", 1280, 720, vsync=True)
        self.set_clear_color(0.0, 0.0, 0.0, 1.0)
        self.font = None
        self.audio = None
        self.background_music = -1
        self.ping_sound = -1
        self.clear_sound = -1
        self.die_sound = -1
        self.blocks: list[dict[str, object]] = []
        try:
            self.font = mx.Font(self.asset_dir / "font.ttf", 24)
            self.intro_background = mx.Sprite(self, self.asset_dir / "intro.png", vertex_shader=str(self.asset_dir / "sprite.vert.spv"), fragment_shader=str(self.asset_dir / "breakout_background.frag.spv"))
            self.game_background = mx.Sprite(self, self.asset_dir / "bg.png", vertex_shader=str(self.asset_dir / "sprite.vert.spv"), fragment_shader=str(self.asset_dir / "breakout_background.frag.spv"))
            self.intro_model = self._load_model("intro_manifest.txt")
            self.paddle_model = self._load_model("texture_manifest.txt")
            self.ball_model = mx.Model(self, self.asset_dir / "better_sphere.obj", textures=self.asset_dir / "moon_manifest.txt", texture_directory=self.asset_dir)
            self.ball_model.shaders(self, self.asset_dir / "breakout_model.vert.spv", self.asset_dir / "breakout_model.frag.spv")
            self._create_blocks()
            if mx.native.has_mixer:
                self.audio = self.add(mx.Sound())
                self.background_music = self.audio.load_music(self.asset_dir / "breakout.ogg")
                self.ping_sound = self.audio.load_wav(self.asset_dir / "ping.wav")
                self.clear_sound = self.audio.load_wav(self.asset_dir / "pop.wav")
                self.die_sound = self.audio.load_wav(self.asset_dir / "die.wav")
                self._ensure_music()
        except Exception:
            if self.font is not None:
                self.font.close()
            self.close()
            raise
        self.screen = "intro"
        self.score = 0
        self.misses = 0
        self.paddle_x = 0.0
        self.ball_x = 0.0
        self.ball_y = -3.0
        self.ball_dx = 2.5
        self.ball_dy = 2.5
        self.ball_rotation = 0.0
        self.ball_stuck = True
        self.grid_rotation = 0.0
        self.grid_y_rotation = 0.0
        self.zoom = DEFAULT_ZOOM
        self.intro_rotation = 0.0
        self.background_started = time.monotonic()
        self.last_frame = self.background_started
        self.reset_game()

    def _load_model(self, manifest: str) -> mx.Model:
        model = mx.Model(self, self.asset_dir / "cube.mxmod.z", textures=self.asset_dir / manifest, texture_directory=self.asset_dir)
        model.shaders(self, self.asset_dir / "breakout_model.vert.spv", self.asset_dir / "breakout_model.frag.spv")
        return model

    def _create_blocks(self) -> None:
        ## @brief Create one independently transformed model for every block.
        ## @details Model uniforms are per model, so sharing one model across
        ## several blocks would make every draw use the final block transform.
        for row in range(BLOCK_ROWS):
            for column in range(BLOCK_COLUMNS):
                model = self._load_model(f"texture{random.randrange(4)}_manifest.txt")
                self.blocks.append({"x": GAME_LEFT + column, "y": 3.0 - row * 0.6, "model": model, "rotation": 0.0, "destroyed": False, "rotating": False})

    def reset_game(self) -> None:
        ## @brief Reset score, lives, ball, paddle, and the randomized block grid.
        self.score = 0
        self.misses = 0
        self.paddle_x = 0.0
        for block in self.blocks:
            block["rotation"] = 0.0
            block["destroyed"] = False
            block["rotating"] = False
        self._reset_ball()

    def _reset_ball(self) -> None:
        self.ball_stuck = True
        self.ball_x = self.paddle_x
        self.ball_y = -3.0
        self.ball_dx = 2.5
        self.ball_dy = 2.5
        self.ball_rotation = 0.0

    @staticmethod
    def _collides(ball_x: float, ball_y: float, object_x: float, object_y: float, width: float, height: float) -> bool:
        closest_x = max(object_x - width * 0.5, min(ball_x, object_x + width * 0.5))
        closest_y = max(object_y - height * 0.5, min(ball_y, object_y + height * 0.5))
        return math.hypot(closest_x - ball_x, closest_y - ball_y) < BALL_RADIUS

    def _move_paddle(self, offset: float) -> None:
        self.paddle_x = max(GAME_LEFT, min(GAME_RIGHT, self.paddle_x + offset))

    def _launch_ball(self) -> None:
        if self.screen == "game" and self.ball_stuck:
            self.ball_stuck = False

    def _ensure_music(self) -> None:
        if self.audio is not None and self.background_music >= 0 and not self.audio.music_playing(self.background_music):
            self.audio.play_music(self.background_music, -1)

    def _play_effect(self, sound_id: int, channel: int) -> None:
        if self.audio is not None and sound_id >= 0:
            self.audio.play(sound_id, channel=channel)

    def on_event(self, event) -> None:
        ## @brief Accept keyboard and pointer controls for the game.
        if event.mouse_motion and self.screen == "game":
            width, _ = self.swapchain_extent
            if width > 0:
                self.paddle_x = max(GAME_LEFT, min(GAME_RIGHT, event.x / width * (GAME_RIGHT - GAME_LEFT) + GAME_LEFT))
            return
        if event.mouse_button_down:
            if self.screen == "intro":
                self.screen = "game"
                self.reset_game()
            elif self.screen == "gameover":
                self.screen = "intro"
            else:
                self._launch_ball()
            return
        if not event.key_down or event.repeat:
            return
        if event.key == mx.KEY_ESCAPE:
            self.quit()
        elif self.screen == "intro" and event.key in (mx.KEY_SPACE, mx.native.key_code("Return")):
            self.screen = "game"
            self.reset_game()
        elif self.screen == "gameover" and event.key == mx.native.key_code("Return"):
            self.screen = "intro"
        elif self.screen == "game":
            if event.key in (mx.KEY_SPACE, mx.native.key_code("Return")):
                self._launch_ball()
            elif event.key == mx.KEY_R:
                self.reset_game()
            elif event.key == KEY_1:
                self.grid_rotation = -33.4
                self.grid_y_rotation = -18.4
            elif event.key == KEY_2:
                self.grid_rotation = -21.4
                self.grid_y_rotation = 31.15
            elif event.key == KEY_3:
                self.grid_rotation = -47.25
                self.grid_y_rotation = 0.85

    def _update(self, delta: float) -> None:
        if self.screen != "game":
            return
        if mx.key_pressed(mx.KEY_LEFT) or mx.key_pressed(KEY_H):
            self._move_paddle(-PADDLE_SPEED * delta)
        if mx.key_pressed(mx.KEY_RIGHT) or mx.key_pressed(KEY_L):
            self._move_paddle(PADDLE_SPEED * delta)
        if mx.key_pressed(KEY_W):
            self.grid_rotation -= ROTATION_SPEED * delta
        if mx.key_pressed(KEY_S):
            self.grid_rotation += ROTATION_SPEED * delta
        if mx.key_pressed(KEY_A):
            self.grid_y_rotation -= ROTATION_SPEED * delta
        if mx.key_pressed(KEY_D):
            self.grid_y_rotation += ROTATION_SPEED * delta
        if mx.key_pressed(KEY_PAGE_UP):
            self.zoom = min(MAX_ZOOM, self.zoom + ZOOM_SPEED * delta)
        if mx.key_pressed(KEY_PAGE_DOWN):
            self.zoom = max(MIN_ZOOM, self.zoom - ZOOM_SPEED * delta)
        if mx.key_pressed(KEY_Q):
            self.grid_rotation = 0.0
            self.grid_y_rotation = 0.0
            self.zoom = DEFAULT_ZOOM
        self.grid_rotation = math.fmod(self.grid_rotation, 360.0)
        self.grid_y_rotation = math.fmod(self.grid_y_rotation, 360.0)
        if self.ball_stuck:
            self.ball_x, self.ball_y = self.paddle_x, -3.0
        else:
            self.ball_x += self.ball_dx * delta
            self.ball_y += self.ball_dy * delta
            self.ball_rotation = (self.ball_rotation + self.ball_dy * delta * 360.0 / (2.0 * math.pi * BALL_RADIUS)) % 360.0
            if self.ball_x <= GAME_LEFT + BALL_RADIUS or self.ball_x >= GAME_RIGHT - BALL_RADIUS:
                self.ball_x = max(GAME_LEFT + BALL_RADIUS, min(GAME_RIGHT - BALL_RADIUS, self.ball_x))
                self.ball_dx = -self.ball_dx
            if self.ball_y >= GAME_TOP - BALL_RADIUS:
                self.ball_y = GAME_TOP - BALL_RADIUS
                self.ball_dy = -abs(self.ball_dy)
            if self.ball_dy < 0.0 and self._collides(self.ball_x, self.ball_y, self.paddle_x, -3.5, 2.0, 0.5):
                distance = max(-1.0, min(1.0, (self.ball_x - self.paddle_x)))
                self.ball_dx, self.ball_dy = distance * BALL_SPEED, abs(self.ball_dy)
                speed = math.hypot(self.ball_dx, self.ball_dy)
                self.ball_dx, self.ball_dy = self.ball_dx / speed * BALL_SPEED, self.ball_dy / speed * BALL_SPEED
                self.ball_y = -3.0
                self._play_effect(self.ping_sound, 0)
            for block in self.blocks:
                if block["destroyed"] or block["rotating"]:
                    continue
                if self._collides(self.ball_x, self.ball_y, float(block["x"]), float(block["y"]), 1.0, 0.5):
                    block["rotating"] = True
                    self.score += 10
                    self.ball_dy = -self.ball_dy
                    self._play_effect(self.clear_sound, 1)
                    break
            if self.ball_y <= GAME_BOTTOM:
                self.misses += 1
                self._play_effect(self.die_sound, 2)
                if self.misses >= MAX_MISSES:
                    self.screen = "gameover"
                else:
                    self._reset_ball()
        for block in self.blocks:
            if block["rotating"]:
                block["rotation"] = float(block["rotation"]) + 360.0 * delta
                if float(block["rotation"]) >= 360.0:
                    block["rotating"] = False
                    block["destroyed"] = True
        if self.blocks and all(bool(block["destroyed"]) for block in self.blocks):
            self.screen = "intro"

    def draw(self) -> None:
        ## @brief Update gameplay and queue the text overlay.
        now = time.monotonic()
        delta = min(0.05, max(0.0, now - self.last_frame))
        self.last_frame = now
        self._ensure_music()
        self._update(delta)
        width, height = self.swapchain_extent
        if self.screen == "intro":
            self.draw_text("Press Enter or click to start", 25, 25, self.font)
        elif self.screen == "gameover":
            text = "Game Over - Press Enter to Restart"
            text_width, _ = self.text_size(text, self.font) or (400, 24)
            self.draw_text(text, max(0, (width - text_width) // 2), max(0, height // 2 - 12), self.font)
        else:
            score = f"Score: {self.score}"
            text_width, _ = self.text_size(score, self.font) or (120, 24)
            self.draw_text(score, max(0, (width - text_width) // 2), 0, self.font)
            self.draw_text(f"Tries: {max(0, MAX_MISSES - self.misses)}", 25, 0, self.font)

    def _render_model(self, model: mx.Model, command_buffer, image_index: int, position: tuple[float, float, float], size: tuple[float, float, float], rotation: float, view: np.ndarray, projection: np.ndarray, model_scale: float = 1.0) -> None:
        matrix = translation(*position) @ rotation_x(math.radians(rotation)) @ scale(size[0] * model_scale, size[1] * model_scale, size[2] * model_scale)
        uniforms = mx.native.ModelUniforms()
        uniforms.model = matrix.reshape(16).tolist()
        uniforms.view = view.reshape(16).tolist()
        uniforms.projection = projection.reshape(16).tolist()
        uniforms.effects = (1.0, 1.0, 1.0, 1.0)
        model.native.update_uniforms(image_index, uniforms)
        model.native.render(command_buffer, image_index, False)

    def on_record_custom_rendering(self, command_buffer, image_index: int) -> None:
        ## @brief Draw the animated background, then the custom 3D scene.
        width, height = self.swapchain_extent
        if width <= 0 or height <= 0:
            return
        background = self.intro_background if self.screen == "intro" else self.game_background
        background.shader_params(time.monotonic() - self.background_started, 0.7, 0.0, 0.0)
        background.draw(0, 0, width=width, height=height)
        self.render_sprite_now(background, command_buffer)
        background.native.clear_queue()
        aspect = width / height
        if self.screen == "intro":
            self.intro_rotation = (self.intro_rotation + 0.8) % 360.0
            self._render_model(self.intro_model, command_buffer, image_index, (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), self.intro_rotation, translation(0.0, 0.0, -10.0), perspective(math.radians(10.0), aspect, 0.1, 100.0))
            return
        view = rotation_x(math.radians(self.grid_rotation)) @ rotation_y(math.radians(self.grid_y_rotation))
        projection = ortho(GAME_LEFT / self.zoom, GAME_RIGHT / self.zoom, GAME_BOTTOM / self.zoom, GAME_TOP / self.zoom)
        self._render_model(self.paddle_model, command_buffer, image_index, (self.paddle_x, -3.5, 0.0), (2.0, 0.5, 1.0), 0.0, view, projection)
        self._render_model(self.ball_model, command_buffer, image_index, (self.ball_x, self.ball_y, 0.0), (BALL_RADIUS * 2.0,) * 3, self.ball_rotation, view, projection, 0.05)
        for block in self.blocks:
            if not block["destroyed"]:
                self._render_model(block["model"], command_buffer, image_index, (float(block["x"]), float(block["y"]), 0.0), (1.0, 0.5, 1.0), float(block["rotation"]), view, projection)


def main() -> None:
    ## @brief Run the MXVK wrapper Breakout sample.
    BreakoutGame().run()


if __name__ == "__main__":
    main()
