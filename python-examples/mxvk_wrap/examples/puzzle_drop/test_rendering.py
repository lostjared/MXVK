"""Optional native rendering regression: MXVK_RENDER_TESTS=1 enables a window."""
import os
from types import SimpleNamespace
import unittest


@unittest.skipUnless(os.environ.get("MXVK_RENDER_TESTS") == "1", "set MXVK_RENDER_TESTS=1 to run the Vulkan window test")
class RenderingTests(unittest.TestCase):
    def test_repeated_cube_textures_and_game_transitions(self):
        # Import lazily so rules tests work without mxvk_ext, NumPy, or Pillow.
        if __package__:
            from .main import KEY, PuzzleDropWindow
            from .game import Cell
        else:
            from main import KEY, PuzzleDropWindow
            from game import Cell
        app = PuzzleDropWindow(960, 540, validation=True)
        original = app.draw
        frames = 0

        def key(name):
            app.on_event(SimpleNamespace(key_down=True, repeat=False, key=KEY[name]))

        def draw():
            nonlocal frames
            if 3 <= frames < 8:
                # Advance the fade clock without depending on monitor refresh.
                app.intro_updated -= .04
            original()
            frames += 1
            if frames == 3:
                key("Return")
            elif frames == 8:
                self.assertFalse(app.intro_active)
                key("2")
                key("X")
                self.assertEqual(app.game.piece.position, 2)
                app.drop(hard=True)
                # Repeated texture slots reproduce the descriptor rewrite bug.
                for x in range(20):
                    app.game.board[-1][x] = Cell(2)
                app.game.board[-1][5:8] = [Cell(2), Cell(3), Cell(4)]
                app.last_process -= .02
            elif frames == 10:
                self.assertGreater(app.game.lines, 0)
            elif frames == 25:
                app.game.game_over = True
            elif frames == 28:
                key("Space")
                self.assertFalse(app.game.game_over)
                self.assertEqual(app.game.difficulty, 1)
                self.assertEqual(app.game.lines, 0)
                key("3")
                self.assertEqual(app.game.difficulty, 2)
                key("1")
                self.assertEqual(app.game.difficulty, 0)
            elif frames == 40:
                app.quit()

        app.draw = draw
        app.run()
        self.assertEqual(frames, 40)
        self.assertTrue(app._closed)


if __name__ == "__main__":
    unittest.main()
