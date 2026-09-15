#!/usr/bin/env python3
## @file main.py
## @brief A small 2D Tetris clone made with @c mxvk_wrap sprites and fonts.
## @details Uses the block and background artwork from @c examples/tetris/data.
## The game is deliberately self-contained: it implements the board, seven
## tetrominoes, rotation, line clearing, scoring, levels, and keyboard input.
## @section tetris_controls Controls
## - Left/Right: move.
## - Up: rotate clockwise.
## - Down: soft drop.
## - Space: hard drop.
## - R: start a new game after game over.
## - Escape: quit.

from __future__ import annotations

from pathlib import Path
import random
import sys
import time

EXAMPLES_DIR = Path(__file__).resolve().parents[3]
if str(EXAMPLES_DIR) not in sys.path:
    sys.path.insert(0, str(EXAMPLES_DIR))

import mxvk_wrap as mx

BOARD_WIDTH = 10
BOARD_HEIGHT = 20
EMPTY = -1
PIECES = (
    ((0, 0), (1, 0), (2, 0), (3, 0)),
    ((0, 0), (0, 1), (1, 1), (2, 1)),
    ((2, 0), (0, 1), (1, 1), (2, 1)),
    ((0, 0), (1, 0), (0, 1), (1, 1)),
    ((1, 0), (2, 0), (0, 1), (1, 1)),
    ((1, 0), (0, 1), (1, 1), (2, 1)),
    ((0, 0), (1, 0), (1, 1), (2, 1)),
)
BLOCK_FILES = (
    "block_ltblue.png", "block_dblue.png", "block_orange.png", "block_yellow.png",
    "block_green.png", "block_purple.png", "block_red.png",
)
CLEAR_FLASH_DURATION = 0.54
CLEAR_FLASH_INTERVAL = 0.09
SIMULATION_STEP = 1.0 / 60.0
MAX_FRAME_DELTA = 0.25


class TetrisGame(mx.App):
    ## @brief A playable 2D Tetris window using @c mxvk_wrap graphics.

    def __init__(self) -> None:
        ## @brief Load Tetris images, create the board, and start the first piece.
        self.asset_dir = Path(__file__).resolve().parent / "data"
        super().__init__("MXVK Wrap 2D Tetris", 960, 720, vsync=True)
        self.font = None
        try:
            self.font = mx.Font(self._font_path(), 24)
            self.background = mx.Sprite(self, self.asset_dir / "psychedelic_background.png", vertex_shader=str(self.asset_dir / "sprite.vert.spv"), fragment_shader=str(self.asset_dir / "background.frag.spv"))
            self.background.enable_extended_uniforms()
            self.blocks = [mx.Sprite(self, self.asset_dir / path) for path in BLOCK_FILES]
            self.clear_block = mx.Sprite(self, self.asset_dir / "block_gray.png")
        except Exception:
            if self.font is not None:
                self.font.close()
            self.close()
            raise
        self.grid: list[list[int]] = []
        self.cells: tuple[tuple[int, int], ...] = ()
        self.piece_color = 0
        self.next_color = 0
        self.piece_x = 0
        self.piece_y = 0
        self.score = 0
        self.lines = 0
        self.level = 1
        self.game_over = False
        self.clearing_rows: tuple[int, ...] = ()
        self.clear_started = 0.0
        self.background_start_time = time.monotonic()
        self.mouse_x = 0.0
        self.mouse_y = 0.0
        self.mouse_pressed = False
        self.simulation_time = 0.0
        self.fall_elapsed = 0.0
        self.frame_accumulator = 0.0
        self.last_frame_time = time.monotonic()
        self.reset()

    def _font_path(self) -> Path:
        ## @brief Return the supplied Tetris font path.
        ## @return The TrueType font stored beside the Tetris block artwork.
        path = self.asset_dir / "font.ttf"
        if not path.is_file():
            raise FileNotFoundError(f"missing Tetris font: {path}")
        return path

    def reset(self) -> None:
        ## @brief Start a fresh game with an empty board and score.
        self.grid = [[EMPTY for _ in range(BOARD_WIDTH)] for _ in range(BOARD_HEIGHT)]
        self.score = 0
        self.lines = 0
        self.level = 1
        self.game_over = False
        self.clearing_rows = ()
        self.clear_started = 0.0
        self.simulation_time = 0.0
        self.fall_elapsed = 0.0
        self.frame_accumulator = 0.0
        self.last_frame_time = time.monotonic()
        self.next_color = random.randrange(len(PIECES))
        self._new_piece()

    def _new_piece(self) -> None:
        ## @brief Select and spawn a random tetromino at the top of the board.
        self.piece_color = self.next_color
        self.next_color = random.randrange(len(PIECES))
        self.cells = PIECES[self.piece_color]
        self.piece_x = 3
        self.piece_y = 0
        if self._collides(self.piece_x, self.piece_y, self.cells):
            self.game_over = True

    def _collides(self, x: int, y: int, cells: tuple[tuple[int, int], ...]) -> bool:
        ## @brief Return whether a piece position overlaps a wall, floor, or block.
        ## @param x Candidate board X coordinate.
        ## @param y Candidate board Y coordinate.
        ## @param cells Tetromino cells relative to @p x and @p y.
        ## @return True when the placement is invalid.
        for cell_x, cell_y in cells:
            board_x = x + cell_x
            board_y = y + cell_y
            if board_x < 0 or board_x >= BOARD_WIDTH or board_y >= BOARD_HEIGHT:
                return True
            if board_y >= 0 and self.grid[board_y][board_x] != EMPTY:
                return True
        return False

    def _move(self, offset_x: int, offset_y: int) -> bool:
        ## @brief Move the active piece when its target placement is valid.
        ## @param offset_x Horizontal cell offset.
        ## @param offset_y Vertical cell offset.
        ## @return True when the piece moved.
        if self.game_over or self._collides(self.piece_x + offset_x, self.piece_y + offset_y, self.cells):
            return False
        self.piece_x += offset_x
        self.piece_y += offset_y
        return True

    def _rotate(self) -> None:
        ## @brief Rotate the active piece clockwise, with simple wall kicks.
        if self.game_over or self.piece_color == 3:
            return
        rotated = tuple((-cell_y, cell_x) for cell_x, cell_y in self.cells)
        minimum_x = min(cell_x for cell_x, _ in rotated)
        minimum_y = min(cell_y for _, cell_y in rotated)
        rotated = tuple((cell_x - minimum_x, cell_y - minimum_y) for cell_x, cell_y in rotated)
        for kick in (0, -1, 1, -2, 2):
            if not self._collides(self.piece_x + kick, self.piece_y, rotated):
                self.cells = rotated
                self.piece_x += kick
                return

    def _lock_piece(self) -> None:
        ## @brief Merge the active piece into the board and clear completed lines.
        for cell_x, cell_y in self.cells:
            board_y = self.piece_y + cell_y
            if board_y >= 0:
                self.grid[board_y][self.piece_x + cell_x] = self.piece_color
        self.clearing_rows = tuple(index for index, row in enumerate(self.grid) if EMPTY not in row)
        if self.clearing_rows:
            self.clear_started = self.simulation_time
            return
        self._new_piece()

    def _finish_line_clear(self) -> None:
        ## @brief Collapse flashing rows, award points, and spawn the next piece.
        cleared = len(self.clearing_rows)
        cleared_rows = set(self.clearing_rows)
        self.grid = [[EMPTY for _ in range(BOARD_WIDTH)] for _ in range(cleared)] + [row for index, row in enumerate(self.grid) if index not in cleared_rows]
        self.lines += cleared
        self.score += (0, 100, 300, 500, 800)[cleared] * self.level
        self.level = self.lines // 10 + 1
        self.clearing_rows = ()
        self.clear_started = 0.0
        self._new_piece()

    def _clear_flash_visible(self) -> bool:
        ## @brief Return whether completed rows should show the flash texture.
        return int((self.simulation_time - self.clear_started) / CLEAR_FLASH_INTERVAL) % 2 == 0

    def _hard_drop(self) -> None:
        ## @brief Drop the active piece to its lowest valid row and lock it.
        if self.game_over:
            return
        while self._move(0, 1):
            pass
        self._lock_piece()

    def _fall_interval(self) -> float:
        ## @brief Return the current automatic drop interval in seconds.
        return max(0.08, 0.75 - (self.level - 1) * 0.06)

    def _simulate_step(self) -> None:
        ## @brief Advance one fixed-rate simulation step independent of rendering.
        if self.clearing_rows:
            if self.simulation_time - self.clear_started >= CLEAR_FLASH_DURATION:
                self._finish_line_clear()
            return
        if self.game_over:
            return
        self.fall_elapsed += SIMULATION_STEP
        while self.fall_elapsed >= self._fall_interval():
            self.fall_elapsed -= self._fall_interval()
            if not self._move(0, 1):
                self._lock_piece()
                return

    def _update(self) -> None:
        ## @brief Run fixed 60 Hz simulation steps to keep game timing frame-rate independent.
        now = time.monotonic()
        elapsed = min(now - self.last_frame_time, MAX_FRAME_DELTA)
        self.last_frame_time = now
        self.frame_accumulator += max(0.0, elapsed)
        while self.frame_accumulator >= SIMULATION_STEP:
            self.frame_accumulator -= SIMULATION_STEP
            self.simulation_time += SIMULATION_STEP
            self._simulate_step()

    def on_event(self, event) -> None:
        ## @brief Handle game controls from MXVK keyboard events.
        ## @param event MXVK event received by @c App.
        if event.mouse_motion:
            self.mouse_x = event.x
            self.mouse_y = event.y
            return
        if event.mouse_button_down or event.mouse_button_up:
            self.mouse_x = event.x
            self.mouse_y = event.y
            self.mouse_pressed = event.mouse_button_down
            return
        if not event.key_down:
            return
        if event.key == mx.KEY_ESCAPE:
            self.quit()
        elif self.clearing_rows:
            return
        elif self.game_over:
            if event.key == mx.KEY_R:
                self.reset()
        elif event.key == mx.KEY_LEFT:
            self._move(-1, 0)
        elif event.key == mx.KEY_RIGHT:
            self._move(1, 0)
        elif event.key == mx.KEY_DOWN:
            if not self._move(0, 1):
                self._lock_piece()
        elif event.key == mx.KEY_UP:
            self._rotate()
        elif event.key == mx.KEY_SPACE:
            self._hard_drop()

    def _draw_cell(self, color: int, board_x: int, board_y: int, left: int, top: int, cell_size: int) -> None:
        ## @brief Queue one block sprite at a board location.
        ## @param color Index into the loaded block sprites.
        ## @param board_x Board column.
        ## @param board_y Board row.
        ## @param left Board left screen coordinate.
        ## @param top Board top screen coordinate.
        ## @param cell_size Block width and height in pixels.
        self.blocks[color].draw(left + board_x * cell_size, top + board_y * cell_size, width=cell_size, height=cell_size)

    def draw(self) -> None:
        ## @brief Draw the background, playfield, active piece, and HUD each frame.
        self._update()
        width, height = self.swapchain_extent
        if width <= 0 or height <= 0:
            return
        self.background.set_mouse(self.mouse_x, self.mouse_y, self.mouse_pressed)
        self.background.set_uniform(0, 0.0, 0.0, float(width), float(height))
        self.background.set_uniform(2, 0.0, time.monotonic() - self.background_start_time, 0.0, 0.0)
        self.background.draw(0, 0, width=width, height=height)
        cell_size = max(16, min((height - 100) // (BOARD_HEIGHT + 1), (width - 260) // (BOARD_WIDTH + 2)))
        board_width = BOARD_WIDTH * cell_size
        board_height = BOARD_HEIGHT * cell_size
        left = max(24, (width - board_width) // 2 - 100)
        top = max(50, (height - board_height) // 2)
        for board_y, row in enumerate(self.grid):
            for board_x, color in enumerate(row):
                if color != EMPTY:
                    if board_y in self.clearing_rows and self._clear_flash_visible():
                        self.clear_block.draw(left + board_x * cell_size, top + board_y * cell_size, width=cell_size, height=cell_size)
                    else:
                        self._draw_cell(color, board_x, board_y, left, top, cell_size)
        if not self.game_over and not self.clearing_rows:
            for cell_x, cell_y in self.cells:
                self._draw_cell(self.piece_color, self.piece_x + cell_x, self.piece_y + cell_y, left, top, cell_size)
        panel_x = left + board_width + 28
        self.draw_text("2D TETRIS", panel_x, top, self.font, mx.Color(255, 245, 180))
        self.draw_text(f"Score  {self.score}", panel_x, top + 54, self.font)
        self.draw_text(f"Lines  {self.lines}", panel_x, top + 88, self.font, mx.Color(180, 225, 255))
        self.draw_text(f"Level  {self.level}", panel_x, top + 122, self.font, mx.Color(180, 255, 190))
        self.draw_text("Next", panel_x, top + 182, self.font, mx.Color(255, 245, 180))
        preview_cell = max(12, cell_size * 3 // 5)
        preview_cells = PIECES[self.next_color]
        preview_min_x = min(cell_x for cell_x, _ in preview_cells)
        preview_min_y = min(cell_y for _, cell_y in preview_cells)
        for cell_x, cell_y in preview_cells:
            self.blocks[self.next_color].draw(panel_x + (cell_x - preview_min_x) * preview_cell, top + 220 + (cell_y - preview_min_y) * preview_cell, width=preview_cell, height=preview_cell)
        self.draw_text("Arrows: move / rotate", panel_x, top + 340, self.font)
        self.draw_text("Space: hard drop", panel_x, top + 374, self.font)
        if self.game_over:
            self.draw_text("GAME OVER", left + cell_size, top + board_height // 2 - 24, self.font, mx.Color(255, 100, 100))
            self.draw_text("Press R to restart", left + cell_size, top + board_height // 2 + 14, self.font, mx.Color(255, 255, 255))


def main() -> None:
    ## @brief Create and run the 2D Tetris application.
    TetrisGame().run()


if __name__ == "__main__":
    main()
