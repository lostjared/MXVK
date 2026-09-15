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
    "Joystick", "KEY_ESCAPE", "Model", "native", "Settings", "Sound", "Sprite", "Sprite3D",
    "Stopwatch",
]

KEY_ESCAPE = native.KEY_ESCAPE
"""Keyboard value used to detect the Escape key in @c App.on_event."""
