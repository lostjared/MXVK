# Tic-Tac-Toe

Tic-Tac-Toe is a mouse-driven board game against a simple computer opponent. The board, marks, and status text are drawn with MXVK sprites and text, while the AI chooses moves directly on the game board array.

## Controls

- `Left mouse click` on a square - place your `X`
- `Click` after a win, loss, or draw - start a new round
- `R` - reset the current game
- `Escape` - quit

## How It Works

The computer checks for immediate wins and blocks, prefers the center, and otherwise picks a random open cell. The UI is resized to match the current swapchain, so the click targets and board layout stay aligned even when the window size changes.

