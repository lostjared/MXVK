"""Puzzle Drop rules, independent of Vulkan and the native extension."""
from dataclasses import dataclass
import random

BOARD_WIDTH = 20
BOARD_HEIGHT = 22
NULL, CLEAR = 0, 1
RED1, GREEN1, BLUE1, MATCH = 2, 5, 8, 11
FALL_SECONDS = (0.86, 0.68, 0.50)


@dataclass
class Block:
    x: int
    y: int
    type: int


@dataclass
class Cell:
    type: int = NULL
    clear_value: int = 0
    flash_counter: int = 0


@dataclass
class Piece:
    blocks: list[Block]
    position: int = 0

    @classmethod
    def new(cls, rng):
        return cls([Block(BOARD_WIDTH // 2, y, rng.randint(RED1, MATCH)) for y in range(3)])

    def shifted(self, dx, dy):
        return Piece([Block(b.x + dx, b.y + dy, b.type) for b in self.blocks], self.position)

    def cycle(self):
        types = [b.type for b in self.blocks]
        for index, block in enumerate(self.blocks):
            block.type = types[(index + 1) % 3]

    def rotated(self, direction):
        result = self.shifted(0, 0)
        target = 1 if direction < 0 else 2
        if self.position not in (0, target):
            return result
        sign = -1 if direction < 0 else 1
        restoring = self.position == target
        for index, block in enumerate(result.blocks):
            block.x += sign * index * (-1 if restoring else 1)
            block.y += index * (1 if restoring else -1)
        result.position = 0 if restoring else target
        return result


class PuzzleGame:
    def __init__(self, difficulty=0, rng=None):
        if difficulty not in range(3):
            raise ValueError("difficulty must be 0, 1, or 2")
        self.difficulty = difficulty
        self.rng = rng if rng is not None else random.Random()
        self.reset()

    def reset(self):
        self.board = [[Cell() for _ in range(BOARD_WIDTH)] for _ in range(BOARD_HEIGHT)]
        self.piece = Piece.new(self.rng)
        self.next_piece = Piece.new(self.rng)
        self.level = 1
        self.lines = 0
        self.game_over = False

    def check_piece(self, piece, dx=0, dy=0):
        return all(0 <= b.x + dx < BOARD_WIDTH and 0 <= b.y + dy < BOARD_HEIGHT and self.board[b.y + dy][b.x + dx].type in (NULL, CLEAR) for b in piece.blocks)

    def move(self, direction):
        if not self.game_over and self.check_piece(self.piece, direction, 0):
            self.piece = self.piece.shifted(direction, 0)

    def rotate(self, direction):
        candidate = self.piece.rotated(direction)
        if not self.game_over and self.check_piece(candidate):
            self.piece = candidate

    def drop(self):
        if self.game_over:
            return
        if self.check_piece(self.piece, 0, 1):
            self.piece = self.piece.shifted(0, 1)
            return
        for block in self.piece.blocks:
            self.board[block.y][block.x] = Cell(block.type)
            if block.y == 0:
                self.game_over = True
        self.piece = self.next_piece
        self.next_piece = Piece.new(self.rng)
        if not self.check_piece(self.piece):
            self.game_over = True

    def hard_drop(self):
        if self.game_over:
            return
        while self.check_piece(self.piece, 0, 1):
            self.piece = self.piece.shifted(0, 1)
        self.drop()

    def proc_blocks(self):
        # The native game marks only the first sequence found on each tick.
        for y in range(BOARD_HEIGHT):
            for x in range(BOARD_WIDTH):
                for dx, dy in ((1, 0), (0, 1), (1, 1), (1, -1)):
                    coords = [(x + dx * i, y + dy * i) for i in range(3)]
                    if not all(0 <= cx < BOARD_WIDTH and 0 <= cy < BOARD_HEIGHT for cx, cy in coords):
                        continue
                    actual = [self.board[cy][cx].type for cx, cy in coords]
                    for start in (RED1, GREEN1, BLUE1):
                        for expected in ((start, start + 1, start + 2), (start + 2, start + 1, start)):
                            if all(a in (e, MATCH) for a, e in zip(actual, expected)):
                                for cx, cy in coords:
                                    self.board[cy][cx] = Cell(CLEAR, 1)
                                self.lines += 1
                                self.level = min(8, 1 + self.lines // 6)
                                return True
        return False

    def proc_move_down(self):
        # One settled cell falls per tick. Flash timers wait until gravity stops.
        for y in range(BOARD_HEIGHT - 2, -1, -1):
            for x in range(BOARD_WIDTH):
                if RED1 <= self.board[y][x].type <= MATCH and self.board[y + 1][x].type == NULL:
                    self.board[y + 1][x], self.board[y][x] = self.board[y][x], Cell()
                    return True
        updated = False
        for row in self.board:
            for cell in row:
                if cell.type == CLEAR:
                    cell.clear_value += 1
                    cell.flash_counter += 1
                    if cell.clear_value > 50:
                        cell.type = NULL
                        cell.clear_value = cell.flash_counter = 0
                    updated = True
        return updated

    def tick(self):
        self.proc_blocks()
        self.proc_move_down()
