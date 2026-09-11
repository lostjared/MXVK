#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import random
import sys
import time
from dataclasses import dataclass
from pathlib import Path

import numpy as np

import mxvk_ext as mxvk


EXAMPLE_DIR = Path(__file__).resolve().parent


def lround(value: float) -> int:
    if (value >= 0.0):
        return math.floor(value + 0.5)

    return math.ceil(value - 0.5)


@dataclass(frozen=True, slots=True)
class Position:
    row: int = 0
    col: int = 0


class Tour:
    BOARD_SIZE = 8
    TOTAL_MOVES = BOARD_SIZE * BOARD_SIZE + 1

    START_X = 100
    START_Y = 30

    CELL_SIZE = 55
    CELL_DRAW_SIZE = 50
    KNIGHT_SIZE = 35

    HORIZONTAL = (2, 1, -1, -2, -2, -1, 1, 2)
    VERTICAL = (-1, -2, -2, -1, 1, 2, 2, 1)

    def __init__(self):
        self.board = [[0 for _ in range(self.BOARD_SIZE)] for _ in range(self.BOARD_SIZE)]
        self.move_sequence: list[Position] = []
        self.knight_pos = Position()
        self.moves = 1
        self.tour_over = False
        self.reset_tour()

    def clear_board(self):
        for row in range(self.BOARD_SIZE):
            for col in range(self.BOARD_SIZE):
                self.board[row][col] = 0

    def is_valid_move(self, position: Position) -> bool:
        return (
            0 <= position.row < self.BOARD_SIZE
            and 0 <= position.col < self.BOARD_SIZE
            and self.board[position.row][position.col] == 0
        )

    def get_degree(self, position: Position) -> int:
        count = 0

        for index in range(8):
            new_row = position.row + self.VERTICAL[index]
            new_col = position.col + self.HORIZONTAL[index]

            if (self.is_valid_move(Position(new_row, new_col))):
                count += 1

        return count

    def solve_knights_tour(self, position: Position, move_count: int) -> bool:
        if (move_count == self.TOTAL_MOVES):
            return True

        next_moves: list[tuple[int, Position]] = []

        for index in range(8):
            next_position = Position(position.row + self.VERTICAL[index], position.col + self.HORIZONTAL[index])

            if (self.is_valid_move(next_position)):
                next_moves.append((self.get_degree(next_position), next_position))

        next_moves.sort(key=lambda item: item[0])

        for _, next_position in next_moves:
            self.board[next_position.row][next_position.col] = move_count
            self.move_sequence.append(next_position)

            if (self.solve_knights_tour(next_position, move_count + 1)):
                return True

            self.board[next_position.row][next_position.col] = 0
            self.move_sequence.pop()

        return False

    def reset_tour(self, start_row: int | None = None, start_col: int | None = None):
        if (start_row is None or start_col is None):
            start_row = random.randrange(self.BOARD_SIZE)
            start_col = random.randrange(self.BOARD_SIZE)

        if (start_row < 0 or start_row >= self.BOARD_SIZE or start_col < 0 or start_col >= self.BOARD_SIZE):
            return

        self.clear_board()

        self.knight_pos = Position(start_row, start_col)
        self.board[self.knight_pos.row][self.knight_pos.col] = 1

        self.move_sequence.clear()
        self.move_sequence.append(self.knight_pos)

        self.solve_knights_tour(self.knight_pos, 2)

        self.moves = 1
        self.tour_over = False

    def reset_tour_from_point(self, x: float, y: float):
        local_x = math.floor(x) - self.START_X
        local_y = math.floor(y) - self.START_Y

        if (local_x < 0 or local_y < 0):
            return

        col = local_x // self.CELL_SIZE
        row = local_y // self.CELL_SIZE

        if (row >= self.BOARD_SIZE or col >= self.BOARD_SIZE or local_x % self.CELL_SIZE >= self.CELL_DRAW_SIZE or local_y % self.CELL_SIZE >= self.CELL_DRAW_SIZE):
            return

        self.reset_tour(row, col)

    def next_move(self):
        if (self.tour_over or self.moves >= len(self.move_sequence)):
            return

        next_position = self.move_sequence[self.moves]

        self.board[self.knight_pos.row][self.knight_pos.col] = -1
        self.knight_pos = next_position
        self.moves += 1
        self.board[self.knight_pos.row][self.knight_pos.col] = self.moves
        self.tour_over = self.moves == len(self.move_sequence)

    def get_moves(self) -> int:
        return self.moves

    def is_tour_over(self) -> bool:
        return self.tour_over

    def draw_board(self, white_cell, red_cell, visited_cell, scale_x: float, scale_y: float):
        for row in range(self.BOARD_SIZE):
            for col in range(self.BOARD_SIZE):
                if (self.board[row][col] == -1):
                    cell = visited_cell
                elif ((row + col) % 2 == 0):
                    cell = white_cell
                else:
                    cell = red_cell

                x = lround((self.START_X + col * self.CELL_SIZE) * scale_x)
                y = lround((self.START_Y + row * self.CELL_SIZE) * scale_y)
                width = lround(self.CELL_DRAW_SIZE * scale_x)
                height = lround(self.CELL_DRAW_SIZE * scale_y)

                cell.draw_rect(x, y, width, height)

    def draw_knight(self, texture, scale_x: float, scale_y: float):
        x = lround((self.START_X + self.knight_pos.col * self.CELL_SIZE + 5) * scale_x)
        y = lround((self.START_Y + self.knight_pos.row * self.CELL_SIZE + 5) * scale_y)
        width = lround(self.KNIGHT_SIZE * scale_x)
        height = lround(self.KNIGHT_SIZE * scale_y)

        texture.draw_rect(x, y, width, height)


class KnightsTourWindow(mxvk.VK_Window):
    DESIGN_WIDTH = 640.0
    DESIGN_HEIGHT = 480.0

    TEXT_OFFSET_X = 15
    TEXT_OFFSET_Y = 5
    TEXT_SIZE = 14

    INTRO_STEP_MS = 15
    INTRO_ALPHA_STEP = 3

    SCREEN_INTRO = 0
    SCREEN_TOUR = 1

    KEY_S = mxvk.key_code("S")
    KEY_SPACE = mxvk.key_code("Space")
    KEY_RETURN = mxvk.key_code("Return")

    def __init__(self, path: str, width: int, height: int, fullscreen: bool, enable_vsync: bool):
        self.asset_root = Path(path).resolve() if path else EXAMPLE_DIR
        self.data_directory = self.asset_root / "data"
        mxvk.set_default_shader_directory(str(self.data_directory))

        super().__init__("Knights Tour", width, height, fullscreen, False, enable_vsync)

        self.font_path = str(self.data_directory / "font.ttf")
        self.screen = self.SCREEN_INTRO
        self.intro_started = time.monotonic()
        self.current_font_size = self.TEXT_SIZE
        self.text_color = mxvk.Color(255, 255, 255, 255)

        self.tour = Tour()

        self.set_clear_color(0.0, 0.0, 0.0, 1.0)
        self.set_font(self.font_path, self.TEXT_SIZE)

        sprite_vertex_shader = str(self.data_directory / "sprite.vert.spv")
        self.intro = self.create_sprite(str(self.data_directory / "logo.png"), sprite_vertex_shader, str(self.data_directory / "fade.frag.spv"))

        self.white_cell = self.make_solid_sprite(255, 255, 255, 255)
        self.red_cell = self.make_solid_sprite(255, 0, 0, 255)
        self.visited_cell = self.make_solid_sprite(0, 0, 0, 255)

        self.knight_sprite = self.create_sprite(str(self.data_directory / "knight.png"), sprite_vertex_shader, str(self.data_directory / "color_key.frag.spv"))

        self.joystick = mxvk.Joystick()
        self.joystick_open = False
        self.joystick_button_count = 0

        self.previous_button_1 = False
        self.previous_button_2 = False

        if (mxvk.Joystick.count() > 0):
            if (self.joystick.open(0)):
                self.joystick_open = True
                self.joystick_button_count = self.joystick.num_buttons()
                print(f"Joystick opened: {self.joystick.name()}")
            else:
                print("Could not open joystick..")

    def close(self):
        if self.joystick_open:
            self.joystick.close()
            self.joystick_open = False
        self.intro = None
        self.white_cell = None
        self.red_cell = None
        self.visited_cell = None
        self.knight_sprite = None
        self.release()

    def make_solid_sprite(self, red: int, green: int, blue: int, alpha: int):
        pixel = np.array([[[red, green, blue, alpha]]], dtype=np.uint8)

        sprite = self.create_sprite(1, 1)
        sprite.update_texture(pixel, 1, 1, 4)

        return sprite

    def event(self, event):
        if (event.key_down):
            if (event.key == mxvk.KEY_ESCAPE):
                self.request_exit()
                return

            if (event.key == self.KEY_S and not event.repeat):
                try:
                    self.save_snapshot("screenshot.png")
                    print("mx: Screenshot captured..")

                except mxvk.MXVKError as exception:
                    print(f"mxvk: screenshot failed: {exception}", file=sys.stderr)

                return

            if (self.screen != self.SCREEN_TOUR):
                return

            if (event.key == self.KEY_SPACE):
                self.tour.next_move()

            elif (event.key == self.KEY_RETURN and not event.repeat):
                self.tour.reset_tour()

            return

        if (self.screen != self.SCREEN_TOUR):
            return

        if (event.mouse_button_down):
            if (event.button == mxvk.MOUSE_BUTTON_LEFT):
                self.reset_tour_from_mouse_position(event.x, event.y)

            elif (event.button == mxvk.MOUSE_BUTTON_RIGHT):
                self.tour.reset_tour()

    def poll_joystick(self):
        if (not self.joystick_open):
            return

        button_1 = False
        button_2 = False

        if (self.joystick_button_count > 1):
            button_1 = bool(self.joystick.button(1))

        if (self.joystick_button_count > 2):
            button_2 = bool(self.joystick.button(2))

        if (self.screen == self.SCREEN_TOUR):
            if (button_1 and not self.previous_button_1):
                self.tour.next_move()

            if (button_2 and not self.previous_button_2):
                self.tour.reset_tour()

        self.previous_button_1 = button_1
        self.previous_button_2 = button_2

    def proc(self):
        self.poll_joystick()

        swap_width, swap_height = self.swapchain_extent

        if (swap_width > 0):
            scale_x = float(swap_width) / self.DESIGN_WIDTH
        else:
            scale_x = 1.0

        if (swap_height > 0):
            scale_y = float(swap_height) / self.DESIGN_HEIGHT
        else:
            scale_y = 1.0

        if (self.screen == self.SCREEN_INTRO):
            self.draw_intro()
            return

        self.update_font(scale_y)

        self.tour.draw_board(self.white_cell, self.red_cell, self.visited_cell, scale_x, scale_y)
        self.tour.draw_knight(self.knight_sprite, scale_x, scale_y)

        if (not self.tour.is_tour_over()):
            self.print_scaled_text("Knights Tour - Space to Move, Click a Square to Restart", self.TEXT_OFFSET_X, self.TEXT_OFFSET_Y, scale_x, scale_y)
            self.print_move_count(scale_x, scale_y)
        else:
            self.print_scaled_text("-[ Tour Complete ]- Press Return to Reset", self.TEXT_OFFSET_X, self.TEXT_OFFSET_Y, scale_x, scale_y)

    def draw_intro(self):
        elapsed_ms = int((time.monotonic() - self.intro_started) * 1000.0)
        fade_steps = elapsed_ms // self.INTRO_STEP_MS
        alpha = max(0, 255 - fade_steps * self.INTRO_ALPHA_STEP)

        if (alpha == 0):
            self.screen = self.SCREEN_TOUR
            return

        swap_width, swap_height = self.swapchain_extent

        self.intro.set_shader_params(float(alpha) / 255.0)
        self.intro.draw_rect(0, 0, int(swap_width), int(swap_height))

    def update_font(self, scale_y: float):
        desired_size = max(1, lround(self.TEXT_SIZE * scale_y))

        if (desired_size == self.current_font_size):
            return

        self.set_font(self.font_path, desired_size)
        self.current_font_size = desired_size

    def print_scaled_text(self, text: str, x: int, y: int, scale_x: float, scale_y: float):
        self.print_text(text, lround(x * scale_x), lround(y * scale_y), self.text_color)

    def print_move_count(self, scale_x: float, scale_y: float):
        text = f"Moves: {self.tour.get_moves()}"
        swap_width, _ = self.swapchain_extent
        right_margin = lround(self.TEXT_OFFSET_X * scale_x)
        x = lround(400.0 * scale_x)

        dimensions = self.get_text_dimensions(text)

        if (dimensions is not None):
            text_width, _ = dimensions
            x = max(0, int(swap_width) - int(text_width) - right_margin)

        self.print_text(text, x, lround(self.TEXT_OFFSET_Y * scale_y), self.text_color)

    def reset_tour_from_mouse_position(self, mouse_x: float, mouse_y: float):
        window_width, window_height = self.swapchain_extent

        if (window_width <= 0 or window_height <= 0):
            return

        design_x = mouse_x * self.DESIGN_WIDTH / float(window_width)
        design_y = mouse_y * self.DESIGN_HEIGHT / float(window_height)

        self.tour.reset_tour_from_point(design_x, design_y)


def parse_resolution(value: str) -> tuple[int, int]:
    try:
        width_text, height_text = value.lower().split("x", 1)
        width = int(width_text)
        height = int(height_text)

    except ValueError as exception:
        raise argparse.ArgumentTypeError("resolution must use WIDTHxHEIGHT, for example 960x720") from exception

    if (width <= 0 or height <= 0):
        raise argparse.ArgumentTypeError("resolution width and height must be greater than zero")

    return width, height


def parse_args():
    parser = argparse.ArgumentParser(description="Knights Tour using the MXVK Python bindings")

    parser.add_argument("-p", "--path", default=str(EXAMPLE_DIR), help="resource root containing the data directory")
    parser.add_argument("-r", "--resolution", type=parse_resolution, default=(960, 720), metavar="WIDTHxHEIGHT", help="window resolution (default: 960x720)")
    parser.add_argument("-f", "--fullscreen", action="store_true", help="start fullscreen")
    parser.add_argument("--enable-vsync", action="store_true", help="enable vertical synchronization")

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    width, height = args.resolution
    window = None

    try:
        window = KnightsTourWindow(args.path, width, height, args.fullscreen, args.enable_vsync)
        window.loop()

    except mxvk.MXVKError as exception:
        print(f"mxvk: Exception: {exception}", file=sys.stderr)
        return 1

    finally:
        if window is not None:
            window.close()

    return 0


if (__name__ == "__main__"):
    raise SystemExit(main())
