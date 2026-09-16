## @file __init__.py
## @brief Friendly Python façade for the @c mxvk_ext nanobind module.
## @details Import the public classes from this module instead of working with
## the lower-level native bindings directly.

from ._native import mxvk as native
from .app import App, Color
from .config import Settings
from .graphics import Font, GpuBuffer, GpuTexture, Model, Sprite, Sprite3D
from .input import Controller, Joystick
from .media import Camera, Sound, Stopwatch

__all__ = [
    "App", "Camera", "Color", "Controller", "Font", "GpuBuffer", "GpuTexture",
    "Joystick", "KEY_DOWN", "KEY_ESCAPE", "KEY_LEFT", "KEY_RIGHT", "KEY_R", "KEY_SPACE", "KEY_UP", "Model", "native", "Settings", "Sound", "Sprite", "Sprite3D",
    "Stopwatch",
    "key_pressed",
]

key_pressed = native.key_pressed
"""Return whether a keyboard key is currently held down."""

KEY_ESCAPE = native.KEY_ESCAPE
"""Keyboard value used to detect the Escape key in @c App.on_event."""
KEY_LEFT = native.key_code("Left")
"""Keyboard value used to detect the Left Arrow key."""
KEY_RIGHT = native.key_code("Right")
"""Keyboard value used to detect the Right Arrow key."""
KEY_UP = native.key_code("Up")
"""Keyboard value used to detect the Up Arrow key."""
KEY_DOWN = native.key_code("Down")
"""Keyboard value used to detect the Down Arrow key."""
KEY_SPACE = native.key_code("Space")
"""Keyboard value used to detect the Space key."""
KEY_R = native.key_code("R")
"""Keyboard value used to detect the R key."""
