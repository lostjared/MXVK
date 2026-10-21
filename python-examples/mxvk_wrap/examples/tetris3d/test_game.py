"""Rule and persistence regression checks; no MXVK extension required."""

from pathlib import Path
import tempfile
import sys
import time
import unittest
from unittest.mock import patch

from .game import EMPTY, Game, HEIGHT, PIECES, WIDTH
from .network import Network, parse_snapshot, snapshot
from .scores import Scores


class GameTests(unittest.TestCase):
    def test_seed_and_spawn(self):
        game = Game(42)
        other = Game(42)
        self.assertEqual((game.color, game.next_color), (other.color, other.next_color))
        self.assertEqual((game.x, game.y), (5, 18))
        self.assertFalse(game.game_over)

    def test_walls_floor_and_hard_drop(self):
        game = Game(1)
        while game.move(-1):
            pass
        self.assertFalse(game.move(-1))
        game.hard_drop()
        self.assertEqual(sum(color != EMPTY for row in game.board for color in row), 4)
        self.assertEqual(game.score, 0)
        self.assertTrue(any(color != EMPTY for color in game.board[0]))

    def test_rotate_kicks_from_wall(self):
        game = Game(1)
        game.cells = ((0, -1), (0, 0), (0, 1), (1, 0))
        game.x, game.y = 0, 8
        game.rotate()
        self.assertFalse(game.collides(game.x, game.y, game.cells))
        self.assertNotEqual(game.cells, ((0, -1), (0, 0), (0, 1), (1, 0)))

    def test_clear_freezes_then_collapses_and_scores(self):
        game = Game(1)
        game.lines = 6
        game.board[0] = [2] * WIDTH
        game.board[1] = [3] * WIDTH
        game.board[2][0] = 4
        game.x, game.y = 5, 3
        game.cells = PIECES[3]
        game.lock()
        self.assertEqual(game.clearing, (0, 1))
        before = (game.x, game.y)
        game.hard_drop()
        self.assertEqual((game.x, game.y), before)
        game.update(0.99)
        self.assertEqual(game.score, 0)
        game.update(0.02)
        self.assertEqual((game.score, game.lines, game.level), (25, 8, 2))
        self.assertEqual(game.board[0][0], 4)
        self.assertFalse(game.clearing)

    def test_gravity_is_independent_of_frame_rate(self):
        fast, slow = Game(4), Game(4)
        for _ in range(180):
            fast.update(1 / 60)
        for _ in range(30):
            slow.update(0.1)
        self.assertEqual((fast.x, fast.y, fast.board), (slow.x, slow.y, slow.board))

    def test_spawn_collision_and_top_out(self):
        game = Game(3)
        game.board[18] = [0] * WIDTH
        game.spawn()
        self.assertTrue(game.game_over)
        game.reset()
        game.y = HEIGHT
        game.lock()
        self.assertTrue(game.game_over)
        self.assertTrue(all(color == EMPTY for row in game.board for color in row))

    def test_snapshot_round_trip_and_validation(self):
        game = Game(7)
        game.hard_drop()
        remote = parse_snapshot(snapshot(game, 5))
        self.assertEqual(remote['board'], [color for row in game.board for color in row])
        self.assertEqual(remote['cells'], game.cells)
        self.assertEqual((remote['color'], remote['x'], remote['y']), (game.color, game.x, game.y))
        with self.assertRaises(ValueError):
            parse_snapshot('S invalid')
        with self.assertRaises(ValueError):
            parse_snapshot(snapshot(game).replace('.', '!'))

    def test_scores_persist_and_trim(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'scores.json'
            scores = Scores(path)
            self.assertFalse(scores.qualifies(0))
            for score in range(1, 13):
                self.assertTrue(scores.add('PLAYER', score))
            loaded = Scores(path)
            self.assertEqual(len(loaded.entries), 10)
            self.assertEqual(loaded.entries[0], ('PLAYER', 12))
            self.assertFalse(loaded.qualifies(1))
            path.write_text('bad data')
            self.assertEqual(Scores(path).entries, [])

    def test_network_exchange_and_disconnect(self):
        host, guest = Network(), Network()
        first, second = Game(1), Game(2)
        second.hard_drop()
        try:
            with patch.object(sys.modules[Network.__module__], 'PORT', 0):
                host.host()
            self.assertIsNotNone(host.listener, host.status)
            port = host.listener.getsockname()[1]
            with patch.object(sys.modules[Network.__module__], 'PORT', port):
                guest.join('127.0.0.1')
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and (host.opponent is None or guest.opponent is None):
                host.update(first)
                guest.update(second)
                time.sleep(0.001)
            self.assertTrue(host.connected, host.status)
            self.assertTrue(guest.connected, guest.status)
            self.assertEqual(host.opponent['board'], [color for row in second.board for color in row])
            self.assertEqual(guest.opponent['cells'], first.cells)
            guest.close()
            host.update(first)
            self.assertFalse(host.connected)
            self.assertIn('disconnected', host.status)
        finally:
            host.close()
            guest.close()


if __name__ == '__main__':
    unittest.main()
