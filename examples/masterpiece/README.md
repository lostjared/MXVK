# MasterPiece

<img width="1440" height="1080" alt="image" src="https://github.com/user-attachments/assets/9eae0d40-7377-4492-ad49-e6772e2b993e" />

MasterPiece is a cleaned-up MXVK port of the original `MasterPiece.SDL` block puzzle game. It keeps the same core loop while replacing the art with updated PNG assets for the intro screen, start menu, cursor, and block tiles.

The example includes:

- an intro screen
- a menu with new game, high scores, credits, and quit options
- a falling-block puzzle mode with score tracking and level speedups
- a high-score table with name entry
- a credits screen

The board is 8 columns by 18 rows. You score by lining up matching colors horizontally, vertically, or diagonally so the matched cells flash, clear, and drop away.

## How To Play

- Move the falling piece left and right to line up colors.
- Use soft drop to speed up placement.
- Rotate the piece colors to fit gaps and build matches.
- Clear enough matches to increase the drop speed.
- If the stack reaches the top, the game ends and qualifying scores can be saved.

## Controls

### Menus

- `Up` / `Down` - move through the menu
- `Enter` - select a menu item
- `Escape` - back out of a screen or quit from the menu

### In Game

- `Left` / `Right` - move the active piece
- `Down` - soft drop
- `A` / `Up` - rotate the piece colors forward
- `S` - rotate the piece colors backward
- `P` - pause and resume
- `Escape` - return to the menu

### High Score Entry

- Type your name to enter text
- `Backspace` - delete the last character
- `Enter` - save the score
- `Escape` - skip saving and return to the score table

## Assets

The example uses the assets in `examples/masterpiece/data/` for sprites, fonts, and the custom intro shader. High scores are stored in `data/scores.dat` at runtime.

## Building

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j
```

## Running

```bash
./run.pl masterpiece
```

You can pass the usual example arguments, for example:

```bash
./run.pl masterpiece -r 1920x1080 -f
```
