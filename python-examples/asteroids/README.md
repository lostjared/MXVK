# Python Asteroids

This is a Python port of `examples/asteroids` (SpaceRox). It retains the
procedural vector ship and asteroids, starfield, countdown and launch sequence,
projectiles, asteroid splitting, explosions, lives, scoring, and weapon
overheating.

Gameplay simulation is capped to the original fixed 60 Hz update rate, so it
does not speed up when VSync is disabled or the swapchain renders above 60 FPS.

Run it after installing the package or by pointing `PYTHONPATH` at a local
`PYTHON_MODULE=ON` build:

```bash
python3 python-examples/asteroids/asteroids.py
```

Use Left/Right to rotate, Up to thrust, Space to fire or restart after game
over, and Escape to quit. Optional arguments are `--width`, `--height`,
`--fullscreen`, and `--vsync`.
