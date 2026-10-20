"""Rules regression tests; run without a window or mxvk_ext."""
import random
import unittest

try:
    from .game import BOARD_HEIGHT, BOARD_WIDTH, CLEAR, MATCH, NULL, Block, Cell, Piece, PuzzleGame
except ImportError:
    from game import BOARD_HEIGHT, BOARD_WIDTH, CLEAR, MATCH, NULL, Block, Cell, Piece, PuzzleGame


class PuzzleDropTests(unittest.TestCase):
    def setUp(self):
        self.game = PuzzleGame(rng=random.Random(1))

    def test_all_sequences_and_wildcard_positions(self):
        for dx, dy in ((1, 0), (0, 1), (1, 1), (1, -1)):
            for start in (2, 5, 8):
                for reverse in (False, True):
                    for wildcard in (None, 0, 1, 2):
                        with self.subTest(direction=(dx, dy), start=start, reverse=reverse, wildcard=wildcard):
                            self.game.reset()
                            values = list(range(start, start + 3))[::(-1 if reverse else 1)]
                            if wildcard is not None:
                                values[wildcard] = MATCH
                            coords = [(5 + i * dx, 5 + i * dy) for i in range(3)]
                            for (x, y), value in zip(coords, values):
                                self.game.board[y][x] = Cell(value)
                            self.assertTrue(self.game.proc_blocks())
                            self.assertEqual(self.game.lines, 1)
                            self.assertTrue(all(self.game.board[y][x].type == CLEAR for x, y in coords))

    def test_identical_or_mixed_colors_do_not_match(self):
        for values in ((2, 2, 2), (2, 6, 10), (3, 2, 4), (NULL, MATCH, MATCH), (CLEAR, MATCH, MATCH)):
            self.game.reset()
            for x, value in enumerate(values):
                self.game.board[-1][x] = Cell(value)
            self.assertFalse(self.game.proc_blocks())

    def test_board_edges_do_not_wrap(self):
        self.game.board[-1][-1] = Cell(2)
        self.game.board[-1][0] = Cell(3)
        self.game.board[-1][1] = Cell(4)
        self.assertFalse(self.game.proc_blocks())

    def test_first_match_only_and_level_progression(self):
        for lines in range(1, 50):
            for x, value in enumerate((2, 3, 4, 5, 6, 7)):
                self.game.board[-1][x] = Cell(value)
            self.game.proc_blocks()
            self.assertEqual(self.game.lines, lines)
            self.assertEqual(self.game.level, min(8, 1 + lines // 6))
            self.assertEqual(self.game.board[-1][3].type, 5)

    def test_gravity_moves_one_cell_and_delays_flash(self):
        self.game.board[-2][0] = Cell(2)
        self.game.board[-2][1] = Cell(5)
        self.game.board[-1][4] = Cell(CLEAR, 1, 0)
        self.game.proc_move_down()
        self.assertEqual(self.game.board[-1][0].type, 2)
        self.assertEqual(self.game.board[-2][1].type, 5)
        self.assertEqual(self.game.board[-1][4].clear_value, 1)
        self.game.proc_move_down()
        for _ in range(49):
            self.game.proc_move_down()
        self.assertEqual(self.game.board[-1][4].type, CLEAR)
        self.game.proc_move_down()
        self.assertEqual(self.game.board[-1][4], Cell())

    def test_clear_cells_allow_piece_but_block_settled_gravity(self):
        self.game.board[3][10] = Cell(CLEAR)
        self.assertTrue(self.game.check_piece(self.game.piece, 0, 1))
        self.game.board[2][10] = Cell(2)
        self.game.proc_move_down()
        self.assertEqual(self.game.board[2][10].type, 2)

    def test_rotation_modes_cycle_and_collision(self):
        piece = Piece([Block(10, i, 2 + i) for i in range(3)])
        self.assertEqual([(b.x, b.y) for b in piece.rotated(-1).blocks], [(10, 0), (9, 0), (8, 0)])
        self.assertEqual([(b.x, b.y) for b in piece.rotated(1).blocks], [(10, 0), (11, 0), (12, 0)])
        self.assertEqual(piece.rotated(1).rotated(1), piece)
        self.assertEqual(piece.rotated(-1).rotated(-1), piece)
        self.assertEqual(piece.rotated(-1).rotated(1), piece.rotated(-1))
        piece.cycle()
        self.assertEqual([b.type for b in piece.blocks], [3, 4, 2])
        self.game.piece = piece.shifted(-10, 0)
        self.game.rotate(-1)
        self.assertEqual(self.game.piece.position, 0)
        self.game.move(-1)
        self.assertEqual(self.game.piece.blocks[0].x, 0)

    def test_hard_drop_locks_and_promotes_preview(self):
        old_piece, next_piece = self.game.piece, self.game.next_piece
        self.game.hard_drop()
        self.assertIs(self.game.piece, next_piece)
        self.assertEqual([self.game.board[y][10].type for y in range(BOARD_HEIGHT - 3, BOARD_HEIGHT)], [b.type for b in old_piece.blocks])

    def test_game_over_and_reset(self):
        self.game.board[3][10] = Cell(2)
        self.game.drop()
        self.assertTrue(self.game.game_over)
        snapshot = repr(self.game.board)
        self.game.hard_drop()
        self.game.drop()
        self.assertEqual(repr(self.game.board), snapshot)
        self.game.difficulty = 2
        self.game.reset()
        self.assertFalse(self.game.game_over)
        self.assertEqual(self.game.difficulty, 2)
        self.assertTrue(all(cell.type == NULL for row in self.game.board for cell in row))

    def test_spawn_collision_below_top_row(self):
        self.game.piece = self.game.piece.shifted(0, BOARD_HEIGHT - 3)
        self.game.board[1][10] = Cell(2)
        self.game.drop()
        self.assertTrue(self.game.game_over)

    def test_boundary_validation(self):
        for dx, dy in ((-11, 0), (10, 0), (0, -1), (0, BOARD_HEIGHT)):
            self.assertFalse(self.game.check_piece(self.game.piece, dx, dy))
        for difficulty in (-1, 3):
            with self.assertRaises(ValueError):
                PuzzleGame(difficulty)


if __name__ == "__main__":
    unittest.main()
