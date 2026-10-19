"""Rule regressions; run with python3 -m unittest discover -s mutatris."""
import random
import unittest

from game import FOCUS_NEXT, GameGrid, PuzzleGame, cell_position


class PieceTests(unittest.TestCase):
    def setUp(self):
        self.grid = GameGrid(24, 23, random.Random(7))
        self.piece = self.grid.piece

    def test_spawn_colors_and_cycle(self):
        self.assertEqual((self.piece.x, self.piece.y), (12, 0))
        self.assertGreater(len(set(self.piece.colors)), 1)
        before = self.piece.colors[:]
        self.piece.shift_colors()
        self.assertEqual(self.piece.colors, before[-1:] + before[:-1])

    def test_rotation_collision_and_all_four_orientations(self):
        self.piece.rotate()
        self.assertEqual(self.piece.direction, 1)
        self.piece.rotate()
        self.assertEqual(self.piece.direction, 2)
        self.piece.rotate()
        self.assertEqual(self.piece.direction, 2)  # Upward at entry is invalid.
        self.piece.y = 4
        self.piece.rotate()
        self.assertEqual(self.piece.direction, 3)
        self.piece.rotate()
        self.assertEqual(self.piece.direction, 0)
        self.grid.at(13, 4).color = 7
        self.piece.rotate()
        self.assertEqual(self.piece.direction, 0)

    def test_drop_locks_resets_and_obeys_floor(self):
        colors = self.piece.colors[:]
        self.assertTrue(self.piece.drop())
        self.assertEqual([self.grid.at(12, y).color for y in (20, 21, 22)], colors)
        self.assertEqual(self.piece.y, 0)

    def test_wall_and_blocked_entry_are_atomic(self):
        self.piece.x = 0
        self.piece.move(-1)
        self.assertEqual(self.piece.x, 0)
        self.grid.at(0, 1).color = 6
        self.assertFalse(self.piece.drop())
        self.assertEqual(self.grid.at(0, 0).color, 0)
        self.assertEqual(self.grid.at(0, 1).color, 6)


class PuzzleTests(unittest.TestCase):
    def test_difficulty_and_clockwise_focus(self):
        for difficulty, timeout in enumerate((1200, 900, 650)):
            game = PuzzleGame(difficulty, seed=1)
            self.assertEqual(game.timeout, timeout)
            self.assertEqual([(g.width, g.height) for g in game.grids], [(24, 23), (25, 28), (24, 22), (25, 28)])
        focus, sequence = 0, []
        for _ in range(4):
            sequence.append(focus)
            focus = FOCUS_NEXT[focus]
        self.assertEqual(sequence, [0, 3, 2, 1])
        self.assertEqual(focus, 0)

    def test_runs_in_each_direction_and_grid(self):
        for index in range(4):
            for dx, dy in ((1, 0), (0, 1), (1, 1), (-1, 1)):
                with self.subTest(grid=index, direction=(dx, dy)):
                    game = PuzzleGame(seed=1)
                    blocks = [game.grids[index].at(10 + dx * step, 10 + dy * step) for step in range(4)]
                    for block in blocks:
                        block.color = 3
                    game.proc_blocks()
                    self.assertTrue(all(block.color < 0 for block in blocks))
                    self.assertEqual((game.score, game.clears), (1, 1))

    def test_visual_matching_crosses_all_adjacent_grids(self):
        runs = [((0, 10, 21), (0, 10, 22), (2, 10, 21)), ((1, 12, 26), (1, 12, 27), (0, 0, 22)), ((0, 23, 22), (3, 12, 27), (3, 12, 26))]
        for run in runs:
            game = PuzzleGame(seed=1)
            for ref in run:
                game.blocks[ref].color = 4
            self.assertTrue(game.clear_visual_run(), run)
            self.assertTrue(all(game.blocks[ref].color < 0 for ref in run))

    def test_seam_bonus_and_animation_duration(self):
        game = PuzzleGame(seed=1)
        refs = ((0, 9, 22), (0, 9, 21), (2, 9, 21), (2, 9, 20))
        for ref in refs:
            game.blocks[ref].color = 6
        game.clear_seams()
        self.assertEqual((game.score, game.clears), (11, 1))
        game.animate_clears(399)
        self.assertTrue(all(game.blocks[ref].color == -24 for ref in refs))
        game.animate_clears(1)
        self.assertTrue(all(game.blocks[ref].color == 0 for ref in refs))

    def test_gravity_moves_one_cell_per_pass_and_waits_for_animation(self):
        game = PuzzleGame(seed=1)
        grid = game.grids[0]
        grid.at(0, 20).color, grid.at(0, 21).color = 5, -1
        game.proc_blocks()
        self.assertEqual(grid.at(0, 20).color, 5)
        game.animate_clears(400)
        game.proc_blocks()
        self.assertEqual(grid.at(0, 20).color, 0)
        self.assertEqual(grid.at(0, 21).color, 5)
        self.assertEqual(grid.at(0, 22).color, 0)

    def test_cascade_and_level_floor(self):
        game = PuzzleGame(seed=1)
        grid = game.grids[0]
        for x, y in ((0, 20), (1, 21), (2, 22)):
            grid.at(x, y).color = 4
        for _ in range(50):
            game.process()
            game.animate_clears(20)
        self.assertGreater(game.clears, 0)
        for _ in range(200):
            game.record_clear([])
        self.assertEqual(game.timeout, 125)
        game.proc_blocks()
        self.assertEqual(game.level, 10)

    def test_visual_neighbor_cache_matches_original_search(self):
        game = PuzzleGame(seed=8)
        rng = random.Random(19)
        refs = list(game.blocks)
        for ref in rng.sample(refs, 300):
            game.blocks[ref].color = rng.randint(1, 7)
        for ref in rng.sample(refs, 40):
            px, py = cell_position(*ref)
            for dx, dy in ((1, 0), (0, -1), (-1, 1)):
                color, best, error = rng.randint(1, 7), None, 1297
                for candidate, block in game.blocks.items():
                    if ref == candidate or block.color != color:
                        continue
                    cx, cy = cell_position(*candidate)
                    ex, ey = abs((cx - px) * 2 - dx * 32), abs((cy - py) * 2 - dy * 32)
                    if ex <= 18 and ey <= 18 and ex * ex + ey * ey < error:
                        best, error = candidate, ex * ex + ey * ey
                self.assertEqual(game.next_visual(ref, color, dx, dy), best)


if __name__ == '__main__':
    unittest.main()
