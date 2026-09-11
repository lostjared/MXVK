#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import sys
import time
from pathlib import Path

import numpy as np

import mxvk_ext as mxvk


EXAMPLE_DIR = Path(__file__).resolve().parent


def matrix_values(matrix: np.ndarray) -> list[float]:
    return matrix.astype(np.float32, copy=False).reshape(16).tolist()


def identity_matrix() -> np.ndarray:
    return np.eye(4, dtype=np.float32)


def rotation_y_matrix(angle: float) -> np.ndarray:
    cosine = math.cos(angle)
    sine = math.sin(angle)

    return np.array([[cosine, 0.0, sine, 0.0], [0.0, 1.0, 0.0, 0.0], [-sine, 0.0, cosine, 0.0], [0.0, 0.0, 0.0, 1.0]], dtype=np.float32)


def scale_matrix(x: float, y: float, z: float) -> np.ndarray:
    return np.array([[x, 0.0, 0.0, 0.0], [0.0, y, 0.0, 0.0], [0.0, 0.0, z, 0.0], [0.0, 0.0, 0.0, 1.0]], dtype=np.float32)


def translation_matrix(x: float, y: float, z: float) -> np.ndarray:
    return np.array([[1.0, 0.0, 0.0, x], [0.0, 1.0, 0.0, y], [0.0, 0.0, 1.0, z], [0.0, 0.0, 0.0, 1.0]], dtype=np.float32)


def normalize_vector(vector: np.ndarray) -> np.ndarray:
    length = float(np.linalg.norm(vector))

    if (length <= 0.0):
        return vector

    return vector / length


def look_at_matrix(eye: tuple[float, float, float], center: tuple[float, float, float], up: tuple[float, float, float]) -> np.ndarray:
    eye_vector = np.array(eye, dtype=np.float32)
    center_vector = np.array(center, dtype=np.float32)
    up_vector = np.array(up, dtype=np.float32)

    forward = normalize_vector(center_vector - eye_vector)
    side = normalize_vector(np.cross(forward, up_vector))
    camera_up = np.cross(side, forward)

    matrix = identity_matrix()

    matrix[0, 0] = side[0]
    matrix[0, 1] = side[1]
    matrix[0, 2] = side[2]
    matrix[0, 3] = -float(np.dot(side, eye_vector))

    matrix[1, 0] = camera_up[0]
    matrix[1, 1] = camera_up[1]
    matrix[1, 2] = camera_up[2]
    matrix[1, 3] = -float(np.dot(camera_up, eye_vector))

    matrix[2, 0] = -forward[0]
    matrix[2, 1] = -forward[1]
    matrix[2, 2] = -forward[2]
    matrix[2, 3] = float(np.dot(forward, eye_vector))

    return matrix


def perspective_matrix(fov_radians: float, aspect: float, near_plane: float, far_plane: float) -> np.ndarray:
    tangent = math.tan(fov_radians * 0.5)

    matrix = np.zeros((4, 4), dtype=np.float32)

    matrix[0, 0] = 1.0 / (aspect * tangent)
    matrix[1, 1] = 1.0 / tangent
    matrix[2, 2] = far_plane / (near_plane - far_plane)
    matrix[2, 3] = (far_plane * near_plane) / (near_plane - far_plane)
    matrix[3, 2] = -1.0

    matrix[1, 1] *= -1.0

    return matrix


class DarkWindow(mxvk.VK_Window):
    TITLE_TEXT = "the Lunatic iS in my heaD"

    def __init__(self, filename: str, path: str, title: str, width: int, height: int, fullscreen: bool, enable_vsync: bool):
        if (not path or path == "."):
            self.asset_root = EXAMPLE_DIR
        else:
            self.asset_root = Path(path).expanduser().resolve()

        self.data_directory = self.asset_root / "data"
        mxvk.set_default_shader_directory(str(self.data_directory))

        self.fallback_width = width
        self.fallback_height = height

        self.mouse_dragging = False
        self.auto_spin_enabled = True

        self.last_mouse_x = 0
        self.last_mouse_y = 0

        self.yaw_degrees = 0.0
        self.pitch_degrees = 0.0
        self.camera_distance = 5.35
        self.mouse_sensitivity = 0.35

        self.auto_spin_radians = 0.0
        self.elapsed_seconds = 0.0

        self.start_time = time.monotonic()
        self.last_frame_time = self.start_time

        self.beam_model = None
        self.model = None
        self._closed = False

        super().__init__(title, width, height, fullscreen, False, enable_vsync)

        try:
            self.set_clear_color(0.0, 0.0, 0.0, 1.0)
            self.set_font(str(self.data_directory / "font.ttf"), 48)

            if (filename):
                model_path = Path(filename).expanduser()
            else:
                model_path = self.data_directory / "pyramid.obj"

            beam_model_path = self.data_directory / "beam.obj"

            dark_vertex_shader = self.data_directory / "dark.vert.spv"
            dark_fragment_shader = self.data_directory / "dark.frag.spv"

            beam_vertex_shader = self.data_directory / "beam3d.vert.spv"
            beam_fragment_shader = self.data_directory / "beam3d.frag.spv"

            self.beam_model = mxvk.AbstractModel()
            self.beam_model.load(self, str(beam_model_path), "", "", 1.0)
            self.beam_model.set_alpha_blending(True)
            self.beam_model.set_shaders(self, str(beam_vertex_shader), str(beam_fragment_shader))

            self.model = mxvk.AbstractModel()
            self.model.load(self, str(model_path), "", "", 1.0)
            self.model.set_alpha_blending(True)
            self.model.set_shaders(self, str(dark_vertex_shader), str(dark_fragment_shader))

        except Exception:
            self.close()
            raise

    def close(self):
        if (self._closed):
            return

        self._closed = True

        try:
            self.wait_idle()

            if (self.beam_model is not None):
                self.beam_model.cleanup(self)
                self.beam_model = None

        finally:
            try:
                if (self.model is not None):
                    self.model.cleanup(self)
                    self.model = None

            finally:
                self.release()

    def event(self, event):
        if (event.key_down):
            if (event.key == mxvk.KEY_ESCAPE):
                self.request_exit()
                return

        if (event.mouse_button_down and event.button == mxvk.MOUSE_BUTTON_LEFT):
            self.mouse_dragging = True
            self.last_mouse_x = int(event.x)
            self.last_mouse_y = int(event.y)
            return

        if (event.mouse_button_up and event.button == mxvk.MOUSE_BUTTON_LEFT):
            self.mouse_dragging = False
            return

        if (event.mouse_motion and self.mouse_dragging):
            x = int(event.x)
            y = int(event.y)

            delta_x = x - self.last_mouse_x
            delta_y = y - self.last_mouse_y

            self.yaw_degrees += float(delta_x) * self.mouse_sensitivity
            self.pitch_degrees += float(delta_y) * self.mouse_sensitivity

            self.pitch_degrees = max(-55.0, min(55.0, self.pitch_degrees))

            self.last_mouse_x = x
            self.last_mouse_y = y
            return

        if (event.mouse_wheel):
            if (event.wheel_y != 0.0):
                delta = float(event.wheel_y)
            else:
                delta = float(event.wheel_ticks_y)

            self.camera_distance -= delta * 0.35
            self.camera_distance = max(2.0, min(10.0, self.camera_distance))
            return

    def on_swapchain_recreated(self):
        if (self.beam_model is not None):
            self.beam_model.resize(self)

        if (self.model is not None):
            self.model.resize(self)

    def proc(self):
        width, _ = self.swapchain_extent

        if (width <= 0):
            width = self.fallback_width

        dimensions = self.get_text_dimensions(self.TITLE_TEXT)

        if (dimensions is not None):
            text_width, _ = dimensions
            text_x = max(0, (int(width) - int(text_width)) // 2)
            self.print_text(self.TITLE_TEXT, text_x, 24, mxvk.Color(255, 255, 255, 255))

    def on_record_custom_rendering(self, command_buffer, image_index: int):
        if (self.model is None or self.beam_model is None):
            return

        now = time.monotonic()
        delta_seconds = now - self.last_frame_time

        self.elapsed_seconds = now - self.start_time
        self.last_frame_time = now

        if (self.auto_spin_enabled):
            self.auto_spin_radians += delta_seconds * 0.55

        width, height = self.swapchain_extent

        if (height > 0):
            aspect = float(width) / float(height)
        else:
            aspect = 1.0

        yaw_radians = math.radians(self.yaw_degrees)
        pitch_radians = math.radians(self.pitch_degrees)

        camera_x = self.camera_distance * math.cos(pitch_radians) * math.sin(yaw_radians)
        camera_y = 0.35 + self.camera_distance * math.sin(pitch_radians)
        camera_z = self.camera_distance * math.cos(pitch_radians) * math.cos(yaw_radians)

        camera_position = (camera_x, camera_y, camera_z)

        view_matrix = look_at_matrix(camera_position, (0.0, 0.35, 0.0), (0.0, 1.0, 0.0))
        projection_matrix = perspective_matrix(math.radians(48.0), aspect, 0.1, 100.0)

        center_x, center_y, center_z = self.model.center_offset
        render_scale = float(self.model.render_scale) * 1.18

        model_matrix = rotation_y_matrix(self.auto_spin_radians)
        model_matrix = model_matrix @ scale_matrix(render_scale, render_scale, render_scale)
        model_matrix = model_matrix @ translation_matrix(center_x, center_y, center_z)

        uniforms = mxvk.ModelUniforms()
        uniforms.model = matrix_values(model_matrix)
        uniforms.view = matrix_values(view_matrix)
        uniforms.projection = matrix_values(projection_matrix)
        uniforms.effects = [
            self.elapsed_seconds,
            0.0,
            0.0,
            0.42,
        ]

        beam_uniforms = mxvk.ModelUniforms()
        beam_uniforms.model = matrix_values(identity_matrix())
        beam_uniforms.view = matrix_values(view_matrix)
        beam_uniforms.projection = matrix_values(projection_matrix)
        beam_uniforms.effects = [
            self.elapsed_seconds,
            self.auto_spin_radians,
            1.0,
            1.0,
        ]

        self.beam_model.update_uniforms(image_index, beam_uniforms)
        self.beam_model.render(command_buffer, image_index, False)

        self.model.update_uniforms(image_index, uniforms)
        self.model.render(command_buffer, image_index, False)


def parse_resolution(value: str) -> tuple[int, int]:
    try:
        width_text, height_text = value.lower().split("x", 1)
        width = int(width_text)
        height = int(height_text)

    except ValueError as exception:
        raise argparse.ArgumentTypeError("resolution must use WIDTHxHEIGHT, for example 1280x720") from exception

    if (width <= 0 or height <= 0):
        raise argparse.ArgumentTypeError("resolution width and height must be greater than zero")

    return width, height


def parse_args():
    parser = argparse.ArgumentParser(description="MXVK Dark Crystal Pyramid Python example")

    parser.add_argument("filename", nargs="?", default="", help="optional OBJ or MXMOD model path")
    parser.add_argument("-i", "--input", dest="input_filename", default="", help="optional OBJ or MXMOD model path")
    parser.add_argument("-p", "--path", default=str(EXAMPLE_DIR), help="resource root containing the data directory")
    parser.add_argument("-r", "--resolution", type=parse_resolution, default=(1280, 720), metavar="WIDTHxHEIGHT", help="window resolution (default: 1280x720)")
    parser.add_argument("-f", "--fullscreen", action="store_true", help="start fullscreen")
    parser.add_argument("--enable-vsync", action="store_true", help="enable vertical synchronization")

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    width, height = args.resolution

    if (args.input_filename):
        filename = args.input_filename
    else:
        filename = args.filename

    window = None

    try:
        window = DarkWindow(filename, args.path, "MXVK Dark Crystal Pyramid", width, height, args.fullscreen, args.enable_vsync)

        window.loop()

    except mxvk.MXVKError as exception:
        print(f"mxvk: Exception: {exception}", file=sys.stderr)
        return 1

    finally:
        if (window is not None):
            window.close()

    return 0


if (__name__ == "__main__"):
    raise SystemExit(main())
