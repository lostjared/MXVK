"""Native Tetris rules, independent of the window and Vulkan bindings.

Board coordinates run upward from the floor, as in the C++ example.
"""

import random

WIDTH, HEIGHT, EMPTY = 10, 20, -1
PIECES = (
    ((-1, 0), (0, 0), (1, 0), (2, 0)),
    ((-1, 0), (0, 0), (1, 0), (-1, 1)),
    ((-1, 0), (0, 0), (1, 0), (1, 1)),
    ((0, 0), (1, 0), (0, 1), (1, 1)),
    ((-1, 0), (0, 0), (0, 1), (1, 1)),
    ((-1, 0), (0, 0), (1, 0), (0, 1)),
    ((-1, 1), (0, 1), (0, 0), (1, 0)),
)
FALL_SECONDS = (0.72, 0.66, 0.60, 0.54, 0.48, 0.43, 0.38, 0.34, 0.30, 0.26, 0.23, 0.20, 0.18, 0.16, 0.14, 0.12)


class Game:
    def __init__(self, seed=None):
        self.random = random.Random(seed)
        self.reset()

    def reset(self):
        self.board = [[EMPTY] * WIDTH for _ in range(HEIGHT)]
        self.score = self.lines = 0
        self.game_over = False
        self.clearing = ()
        self.clear_elapsed = self.fall_elapsed = 0.0
        self.next_color = self.random.randrange(7)
        self.spawn()

    @property
    def level(self):
        return self.lines // 8 + 1

    @property
    def interval(self):
        return FALL_SECONDS[min(self.level - 1, len(FALL_SECONDS) - 1)]

    def spawn(self):
        self.color = self.next_color
        self.next_color = self.random.randrange(7)
        self.cells = PIECES[self.color]
        self.x, self.y = WIDTH // 2, HEIGHT - 2
        self.fall_elapsed = 0.0
        self.game_over = self.collides(self.x, self.y, self.cells)

    def collides(self, x, y, cells):
        return any(x + dx < 0 or x + dx >= WIDTH or y + dy < 0 or (y + dy < HEIGHT and self.board[y + dy][x + dx] != EMPTY) for dx, dy in cells)

    def move(self, dx, dy=0):
        if self.game_over or self.clearing or self.collides(self.x + dx, self.y + dy, self.cells):
            return False
        self.x += dx
        self.y += dy
        return True

    def rotate(self):
        if self.game_over or self.clearing:
            return
        rotated = tuple((-dy, dx) for dx, dy in self.cells)
        for kick in (0, -1, 1):
            if not self.collides(self.x + kick, self.y, rotated):
                self.x += kick
                self.cells = rotated
                return

    def soft_drop(self):
        if not self.game_over and not self.clearing and not self.move(0, -1):
            self.lock()

    def hard_drop(self):
        if self.game_over or self.clearing:
            return
        while self.move(0, -1):
            pass
        self.lock()

    def lock(self):
        # A piece protruding above the board ends the game rather than losing cells.
        if any(self.y + dy >= HEIGHT for _, dy in self.cells):
            self.game_over = True
            return
        for dx, dy in self.cells:
            self.board[self.y + dy][self.x + dx] = self.color
        self.fall_elapsed = 0.0
        self.clearing = tuple(y for y, row in enumerate(self.board) if EMPTY not in row)
        self.clear_elapsed = 0.0
        if not self.clearing:
            self.spawn()

    def update(self, delta):
        if self.game_over:
            return
        if self.clearing:
            self.clear_elapsed += delta
            if self.clear_elapsed >= 1.0:
                count = len(self.clearing)
                self.board = [row for y, row in enumerate(self.board) if y not in self.clearing] + [[EMPTY] * WIDTH for _ in range(count)]
                self.score += count * 10 + (5 if count > 1 else 0)
                self.lines += count
                self.clearing = ()
                self.spawn()
            return
        self.fall_elapsed += delta
        while self.fall_elapsed >= self.interval:
            self.fall_elapsed -= self.interval
            old_y = self.y
            self.soft_drop()
            if self.clearing or self.game_over or self.y >= old_y:
                break

    def visible_blocks(self):
        flash = int(self.clear_elapsed / 0.12) % 2 == 0
        for y, row in enumerate(self.board):
            for x, color in enumerate(row):
                if color != EMPTY and (y not in self.clearing or flash):
                    yield x, y, color
        if not self.clearing and not self.game_over:
            for dx, dy in self.cells:
                yield self.x + dx, self.y + dy, self.color
