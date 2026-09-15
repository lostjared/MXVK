## @file graphics.py
## @brief Simple drawable, model, and GPU-resource wrappers.

from pathlib import Path
from typing import TYPE_CHECKING, Sequence

from ._native import mxvk

if TYPE_CHECKING:
    from .app import App


class Font:
    ## @brief A reusable TrueType font for individual text draw calls.
    ## @details Supplying this font to @c App.draw_text does not change or
    ## reload the window's default font configured with @c App.set_font.

    def __init__(self, path: str | Path, size: int) -> None:
        ## @brief Load a font file at the requested point size.
        ## @param path Path to a TrueType or OpenType font file.
        ## @param size Font size in points.
        self.native = mxvk.Font(str(path), size)

    @property
    def valid(self) -> bool:
        ## @brief Return whether the native font was loaded successfully.
        return self.native is not None and self.native.valid()

    def close(self) -> None:
        ## @brief Drop this font's native handle.
        self.native = None


class Sprite:
    ## @brief A 2D image that can be queued for drawing each frame.

    def __init__(self, app: "App", image: str | Path | None = None, *, width: int = 0, height: int = 0, vertex_shader: str = "", fragment_shader: str = "") -> None:
        ## @brief Create an image sprite or an empty dynamic texture.
        ## @param app The owning application.
        ## @param image Image file to load; omit for an empty sprite.
        ## @param width Empty texture width.
        ## @param height Empty texture height.
        ## @param vertex_shader Optional vertex SPIR-V path.
        ## @param fragment_shader Optional fragment SPIR-V path.
        if image is not None:
            self.native = app.create_sprite(str(image), vertex_shader, fragment_shader)
        elif width > 0 and height > 0:
            self.native = app.create_sprite(width, height, vertex_shader, fragment_shader)
        else:
            raise ValueError("provide image or positive width and height")
        app.add(self)

    def draw(self, x: int = 0, y: int = 0, *, width: int | None = None, height: int | None = None, scale: float = 1.0, rotation: float = 0.0) -> None:
        ## @brief Queue this sprite for the current frame.
        ## @param x Left pixel coordinate.
        ## @param y Top pixel coordinate.
        ## @param width Optional output width; requires @p height too.
        ## @param height Optional output height; requires @p width too.
        ## @param scale Uniform scale used when no output rectangle is supplied.
        ## @param rotation Rotation in radians used when no output rectangle is supplied.
        if width is not None or height is not None:
            if width is None or height is None:
                raise ValueError("width and height must be supplied together")
            self.native.draw_rect(x, y, width, height)
        elif rotation != 0.0:
            self.native.draw_rotated(x, y, scale, scale, rotation)
        elif scale != 1.0:
            self.native.draw(x, y, scale, scale)
        else:
            self.native.draw(x, y)

    def update(self, pixels: object, width: int, height: int, *, pitch: int = 0) -> None:
        ## @brief Replace this sprite's pixels with a contiguous RGBA8 array.
        ## @param pixels NumPy-compatible RGBA8 pixel array.
        ## @param width Pixel width.
        ## @param height Pixel height.
        ## @param pitch Bytes per row; zero uses @c width * 4.
        self.native.update_texture(pixels, width, height, pitch)

    def shader_params(self, first: float = 0.0, second: float = 0.0, third: float = 0.0, fourth: float = 0.0) -> None:
        ## @brief Set the four generic shader parameters for this sprite.
        self.native.set_shader_params(first, second, third, fourth)

    def close(self) -> None:
        ## @brief Drop this sprite's native handle before its app is released.
        ## @details MXVK owns sprites through the window; releasing this Python
        ## handle first prevents a Python/native reference cycle at shutdown.
        self.native = None


class Sprite3D:
    ## @brief A billboard sprite rendered through MXVK's 3D command callback.

    def __init__(self, app: "App", image: str | Path, *, vertex_shader: str = "", fragment_shader: str = "") -> None:
        ## @brief Load a 3D sprite owned by @p app.
        self.native = app.create_sprite3d(str(image), vertex_shader, fragment_shader)
        app.add(self)

    def queue(self, position: Sequence[float], size: Sequence[float], color: Sequence[float] = (1.0, 1.0, 1.0, 1.0), rotation: float = 0.0) -> None:
        ## @brief Queue one billboard draw.
        self.native.draw(tuple(position), tuple(size), tuple(color), rotation)

    def close(self) -> None:
        ## @brief Release 3D sprite resources and drop its native handle.
        if self.native is not None:
            self.native.cleanup()
            self.native = None


class Model:
    ## @brief A loadable 3D model with one-call setup.

    def __init__(self, app: "App", path: str | Path, *, textures: str | Path = "", texture_directory: str | Path = "", scale: float = 1.0) -> None:
        ## @brief Load a model and retain its application lifetime.
        self._app = app
        self.native = mxvk.AbstractModel()
        self.native.load(app, str(path), str(textures), str(texture_directory), scale)
        app.add(self)

    def shaders(self, app: "App", vertex: str | Path, fragment: str | Path) -> None:
        ## @brief Select custom model shaders.
        self.native.set_shaders(app, str(vertex), str(fragment))

    def cleanup(self, app: "App") -> None:
        ## @brief Explicitly free model GPU resources before closing @p app.
        if self.native is not None:
            self.native.cleanup(app)
            self.native = None

    def close(self) -> None:
        ## @brief Free model GPU resources and drop its native handle.
        if self.native is not None:
            self.native.cleanup(self._app)
            self.native = None


class GpuBuffer:
    ## @brief A small host-writable uniform or storage buffer.

    def __init__(self, app: "App", size: int, *, storage: bool = False) -> None:
        ## @brief Allocate a buffer with @p size bytes.
        self.native = mxvk.create_storage_buffer(app, size) if storage else mxvk.create_uniform_buffer(app, size)
        app.add(self)

    def write(self, data: bytes) -> None:
        ## @brief Upload bytes to the buffer.
        self.native.write(data)

    def close(self) -> None:
        ## @brief Release the underlying Vulkan buffer early.
        if self.native is not None:
            self.native.close()
            self.native = None


class GpuTexture:
    ## @brief A GPU texture loaded from an image file.

    def __init__(self, app: "App", path: str | Path) -> None:
        ## @brief Load @p path into an application-owned GPU texture.
        self.native = mxvk.load_texture(app, str(path))
        app.add(self)

    def close(self) -> None:
        ## @brief Release the underlying Vulkan texture early.
        if self.native is not None:
            self.native.close()
            self.native = None
