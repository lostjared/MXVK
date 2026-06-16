# MasterPiece

This is a cleaned-up MXVK port of the original `MasterPiece.SDL` block puzzle game. The port uses new 1440x1080 PNG art for the intro screen, start menu, cursor, block tiles, and game background.

## Controls

- `Up` / `Down` - move through the menu
- `Enter` - select menu items, confirm score entry
- `Escape` - back out of a screen or quit from the menu
- `Left` / `Right` - move the active piece
- `Down` - soft drop
- `A` / `Up` - rotate the piece colors forward
- `S` - rotate the piece colors backward
- `P` - pause and resume

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
