"""Mutatris rules translated from the C++ example, independent of Vulkan."""

from dataclasses import dataclass
from functools import lru_cache
import random

GRID_SIZES = ((24, 23), (25, 28), (24, 22), (25, 28))
DIRECTIONS = ((1, 0), (0, 1), (1, 1), (-1, 1))
OFFSETS = (((0, 0), (0, 1), (0, 2)), ((0, 0), (1, 0), (2, 0)), ((0, 0), (-1, 0), (-2, 0)), ((0, 0), (0, -1), (0, -2)))
FOCUS_NEXT = (3, 0, 1, 2)


def cell_position(index, x, y):
    if index == 0:
        return 448 + x * 16, y * 16
    if index == 1:
        return y * 16, 160 + x * 16
    if index == 2:
        return 448 + x * 16, 368 + (21 - y) * 16
    return 832 + (27 - y) * 16, 160 + (24 - x) * 16


@dataclass
class Block:
    color: int = 0
    clear_elapsed: float = 0


class Piece:
    def __init__(self, grid, rng):
        self.grid, self.rng = grid, rng
        self.reset()

    def reset(self):
        self.x, self.y, self.direction = self.grid.width // 2, 0, 0
        self.colors = [self.rng.randint(1, 7) for _ in range(3)]
        while len(set(self.colors)) == 1:
            self.colors = [self.rng.randint(1, 7) for _ in range(3)]

    def cells(self, x=None, y=None):
        x = self.x if x is None else x
        y = self.y if y is None else y
        return [(x + dx, y + dy) for dx, dy in OFFSETS[self.direction]]

    def check_location(self, x, y):
        return all((block := self.grid.at(cx, cy)) is not None and block.color == 0 for cx, cy in self.cells(x, y))

    def shift_colors(self):
        self.colors = self.colors[-1:] + self.colors[:-1]

    def move(self, dx):
        if self.check_location(self.x + dx, self.y):
            self.x += dx

    def rotate(self):
        previous = self.direction
        self.direction = (previous + 1) % 4
        if not self.check_location(self.x, self.y):
            self.direction = previous

    def lock(self):
        # Validate the whole placement before writing any cells.
        if not self.check_location(self.x, self.y):
            return False
        for (x, y), color in zip(self.cells(), self.colors):
            block = self.grid.at(x, y)
            block.color, block.clear_elapsed = color, 0
        self.reset()
        return True

    def move_down(self):
        if self.check_location(self.x, self.y + 1):
            self.y += 1
            return False
        return self.lock()

    def drop(self):
        while self.check_location(self.x, self.y + 1):
            self.y += 1
        return self.lock()


class GameGrid:
    def __init__(self, width, height, rng):
        self.width, self.height = width, height
        self.cells = [Block() for _ in range(width * height)]
        self.piece = Piece(self, rng)

    def at(self, x, y):
        return self.cells[y * self.width + x] if 0 <= x < self.width and 0 <= y < self.height else None


@lru_cache(maxsize=1)
def visual_neighbors():
    """Cache the original 18-unit doubled-coordinate tolerance and tie order.

    Candidate order remains grid/y/x for equal distances. Matching chooses the
    closest cell *of the requested color*, including cells across grid seams.
    """
    positions = {}
    refs = []
    for index, (width, height) in enumerate(GRID_SIZES):
        for y in range(height):
            for x in range(width):
                ref = (index, x, y)
                px, py = cell_position(*ref)
                refs.append(ref)
                positions.setdefault((px * 2, py * 2), []).append(ref)
    neighbors = {}
    for ref in refs:
        px, py = cell_position(*ref)
        for dx, dy in DIRECTIONS + tuple((-x, -y) for x, y in DIRECTIONS):
            expected_x, expected_y = px * 2 + dx * 32, py * 2 + dy * 32
            candidates = []
            # All doubled positions are even; retain exact original tolerance.
            for ox in range(-18, 19, 2):
                for oy in range(-18, 19, 2):
                    for candidate in positions.get((expected_x + ox, expected_y + oy), ()):
                        if candidate != ref:
                            candidates.append((ox * ox + oy * oy, candidate))
            candidates.sort(key=lambda item: (item[0], item[1][0], item[1][2], item[1][1]))
            neighbors[ref, dx, dy] = tuple(candidate for _, candidate in candidates)
    return neighbors


class PuzzleGame:
    def __init__(self, difficulty=0, line_sound=None, seed=None):
        self.rng = random.Random(seed)
        self.grids = [GameGrid(w, h, self.rng) for w, h in GRID_SIZES]
        self.score = self.clears = self.level = 0
        self.timeout = (1200, 900, 650)[difficulty]
        self.line_sound = line_sound or (lambda: None)
        self.neighbors = visual_neighbors()
        self.blocks = {(i, x, y): grid.at(x, y) for i, grid in enumerate(self.grids) for y in range(grid.height) for x in range(grid.width)}

    def record_clear(self, blocks, bonus=0):
        for block in blocks:
            block.color, block.clear_elapsed = -1, 0
        self.score += 1 + bonus
        self.clears += 1
        if self.clears % 8 == 0:
            self.timeout = max(125, self.timeout - 100)
        self.line_sound()
        if bonus:
            self.line_sound()

    def next_visual(self, ref, color, dx, dy):
        return next((candidate for candidate in self.neighbors[ref, dx, dy] if self.blocks[candidate].color == color), None)

    def clear_visual_run(self):
        for ref, block in self.blocks.items():
            if block.color <= 0:
                continue
            for dx, dy in DIRECTIONS:
                if self.next_visual(ref, block.color, -dx, -dy) is not None:
                    continue
                run, current = [ref], ref
                while (current := self.next_visual(current, block.color, dx, dy)) is not None:
                    if current in run:
                        break
                    run.append(current)
                if len(run) >= 3:
                    self.record_clear([self.blocks[cell] for cell in run])
                    return True
        return False

    def clear_run(self, grid, x, y, dx, dy):
        start = grid.at(x, y)
        if start.color <= 0:
            return False
        previous = grid.at(x - dx, y - dy)
        if previous is not None and previous.color == start.color:
            return False
        run = []
        while (block := grid.at(x, y)) is not None and block.color == start.color:
            run.append(block)
            x, y = x + dx, y + dy
        if len(run) < 3:
            return False
        self.record_clear(run)
        return True

    def clear_seams(self):
        top, bottom = self.grids[0], self.grids[2]
        for x in range(24):
            a, b, c, d = top.at(x, 22), top.at(x, 21), bottom.at(x, 21), bottom.at(x, 20)
            for run in ((a, b, c, d), (c, a, d, b)):
                if run[0].color > 0 and run[0].color == run[1].color == run[2].color:
                    fourth = run[3].color == run[0].color
                    self.record_clear(run if fourth else run[:3], 10 if fourth else 0)
                    return

    def proc_blocks(self):
        self.level = min(10, max(0, (1200 - self.timeout) // 100))
        if self.clear_visual_run():
            return
        for grid in self.grids:
            for x in range(grid.width):
                for y in range(grid.height):
                    if grid.at(x, y).color <= 0:
                        continue
                    for dx, dy in ((0, 1), (1, 0), (1, 1), (-1, 1)):
                        if self.clear_run(grid, x, y, dx, dy):
                            return
        self.clear_seams()
        # Like the original, move only one settled block per processing pass.
        for grid in self.grids:
            for x in range(grid.width):
                for y in range(grid.height - 2, -1, -1):
                    current = grid.at(x, y)
                    if current.color <= 0:
                        continue
                    below = grid.at(x, y + 1)
                    if below.color == 0:
                        current.color, below.color = below.color, current.color
                        return

    def process(self):
        for _ in range(4):
            self.proc_blocks()

    def animate_clears(self, elapsed_ms):
        for block in self.blocks.values():
            if block.color < 0:
                block.clear_elapsed += elapsed_ms
                if block.clear_elapsed >= 400:
                    block.color, block.clear_elapsed = 0, 0
                else:
                    block.color = -(min(23, int(block.clear_elapsed * 24 / 400)) + 1)
