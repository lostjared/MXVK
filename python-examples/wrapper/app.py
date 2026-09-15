## @file app.py
## @brief Application and window helpers.

from pathlib import Path
from typing import Any

from ._native import mxvk

Color = mxvk.Color
"""Alias for MXVK's RGBA color class."""


class App(mxvk.Window):
    ## @brief A simple callback-driven MXVK application.
    ## @details Subclass this class and override @c draw and optionally
    ## @c on_event.  Managed sprites and models are released before the window.

    def __init__(self, title: str = "MXVK", width: int = 1280, height: int = 720, *, fullscreen: bool = False, vsync: bool = True, validation: bool = False, shader_directory: str | Path | None = None) -> None:
        ## @brief Create a Vulkan window with practical Python defaults.
        ## @param title Text displayed in the window title bar.
        ## @param width Initial window width in pixels.
        ## @param height Initial window height in pixels.
        ## @param fullscreen Start in fullscreen mode when true.
        ## @param vsync Synchronize presentation with the display when true.
        ## @param validation Enable Vulkan validation layers when true.
        ## @param shader_directory Optional directory containing shared shaders.
        if shader_directory is not None:
            mxvk.set_default_shader_directory(str(shader_directory))
        present_mode = mxvk.PresentModePreference.vsync if vsync else mxvk.PresentModePreference.low_latency
        super().__init__(title, width, height, fullscreen, validation, present_mode, mxvk.RuntimeMode.windowed)
        self._managed: list[Any] = []
        self._closed = False

    def draw(self) -> None:
        ## @brief Draw one frame.
        ## @details Override this method; queue sprite drawing here.
        pass

    def on_event(self, event: mxvk.Event) -> None:
        ## @brief Receive one SDL input event.
        ## @param event Read-only MXVK event data.
        pass

    def proc(self) -> None:
        ## @brief Native render-loop callback that forwards to @c draw.
        self.draw()

    def event(self, event: mxvk.Event) -> None:
        ## @brief Native event-loop callback that forwards to @c on_event.
        self.on_event(event)

    def add(self, resource: Any) -> Any:
        ## @brief Keep a wrapper resource alive for the application's lifetime.
        ## @param resource A wrapper created for this application.
        ## @return The supplied resource, for convenient inline use.
        self._managed.append(resource)
        return resource

    def run(self) -> None:
        ## @brief Run until @c quit is called or the window is closed.
        try:
            self.loop()
        finally:
            self.close()

    def quit(self) -> None:
        ## @brief Request that the render loop exit after the current frame.
        self.request_exit()

    def close(self) -> None:
        ## @brief Safely release managed objects and Vulkan window resources.
        if self._closed:
            return
        self._closed = True
        self._managed.clear()
        self.wait_idle()
        self.release()

    def __enter__(self) -> "App":
        ## @brief Enter a context manager owning this application.
        return self

    def __exit__(self, *_: object) -> None:
        ## @brief Close the application when its context exits.
        self.close()
