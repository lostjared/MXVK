# Acid Drop

Acid Drop is a falling-block puzzle inspired by color-matching and line-clearing arcade games. It has an intro flow, a start menu, high scores, options, credits, and a name-entry screen for new records.

## Controls

### Intro and menus

- `Enter` - advance from the intro screen
- `Up` / `Down` - move through menu items
- `Space` on the start screen - open high scores
- `Enter` - select the highlighted menu item
- `Escape` - return to the start menu or quit from the intro

### In game

- `Left` / `Right` - move the falling block
- `Down` - drop it faster
- `Up` - shift the block color upward
- `Space` - rotate the block
- `Z` - shift the block color downward
- `P` - pause or resume
- `Escape` - return to the start menu

### Options

- `Up` / `Down` - change the selected option
- `Left` / `Right` - adjust difficulty or toggle shader effects
- `Enter` - leave the options screen

### High score entry

- Type text to enter your name
- `Backspace` - delete the last character
- `Enter` - save the score
- `Escape` - cancel entry

## How It Works

The game drops a three-segment block into a grid, checks for matching runs, and awards bonuses for longer clears. Visual effects are driven by selectable shaders, and the different screens are all handled inside one state machine so the same build can switch cleanly between play, options, scores, and credits.

