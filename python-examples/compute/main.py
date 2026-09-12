#!/usr/bin/env python3

import argparse
import time
from pathlib import Path

import mxvk_ext as mxvk


EXAMPLE_DIR = Path(__file__).resolve().parent
DATA_DIR = EXAMPLE_DIR / "data"


class SpriteWindow(mxvk.VK_Window):
    def __init__(self, filename: str, width: int, height: int, fullscreen: bool, enable_vsync: bool) -> None:
        super().__init__("VK_Example", width, height, fullscreen, False, enable_vsync)

        self.fallback_width = width
        self.fallback_height = height
        self._closed = False
        self.start_time = time.monotonic()
        self.last_time = self.start_time
        self.frame_count = 0
        self.mouse_x = 0.0
        self.mouse_y = 0.0
        self.mouse_pressed = False
        self.alpha = 1.0
        self.alpha_direction = 1.0
        self.amp = 0.0
        self.iamp = 0.0
        self.amp_peak = 0.0
        self.amp_rms = 0.0
        self.amp_smooth = 0.0
        self.amp_low = 0.0
        self.amp_mid = 0.0
        self.amp_high = 0.0
        self.sample_rate = 44100.0
        self.set_font(str(DATA_DIR / "font.ttf"), 24)
        self.sprite = self.create_sprite(str(DATA_DIR / "intro.png"), str(DATA_DIR / "sprite.vert.spv"), str(DATA_DIR / "fragment.frag.spv"))
        compute_path = DATA_DIR / filename
        info = mxvk.inspect_spirv_file(str(compute_path))

        if(info.stage != mxvk.ShaderStage.compute):
            raise RuntimeError(f"{compute_path} is not a compute shader")

        effect = mxvk.PostProcessingEffect()
        effect.fragment_shader_path = str(compute_path)
        effect.stage = mxvk.ShaderStage.compute
        effect.time_enabled = False
        self.effects = [effect]
        self.post_sprites = self.attach_post_processing_shaders(self.effects)
        self.compute_sprite = self.post_sprites[0]
        self.compute_sprite.set_custom_uniforms([1.0, 0.018, 0.35, 0.12])
        self.set_post_processing_enabled(True)

    def close(self) -> None:
        if(self._closed):
            return

        self._closed = True

        try:
            self.detach_post_processing_shader()
            self.compute_sprite = None
            self.post_sprites = None
            self.effects = None
            self.sprite = None
        finally:
            self.release()

    def update_uniforms(self, width: int, height: int) -> None:
        now = time.monotonic()
        elapsed = now - self.start_time
        delta_time = now - self.last_time
        self.last_time = now

        if(delta_time > 0.0):
            frame_rate = 1.0 / delta_time
        else:
            frame_rate = 0.0

        self.alpha += 0.1 * self.alpha_direction

        if(self.alpha >= 6.0):
            self.alpha = 6.0
            self.alpha_direction = -1.0
        elif(self.alpha <= 1.0):
            self.alpha = 1.0
            self.alpha_direction = 1.0

        self.compute_sprite.set_mouse_state(self.mouse_x, self.mouse_y, 1.0 if self.mouse_pressed else 0.0, 0.0)
        self.compute_sprite.set_uniform0(self.alpha, elapsed, float(width), float(height))
        self.compute_sprite.set_uniform1(delta_time, self.amp, self.iamp, frame_rate)
        self.compute_sprite.set_uniform2(float(self.frame_count), elapsed, self.sample_rate, self.amp_peak)
        self.compute_sprite.set_uniform3(0.0, 0.0, self.amp_rms, self.amp_smooth)
        self.compute_sprite.set_audio_bands(self.amp_low, self.amp_mid, self.amp_high, 0.0)

    def proc(self) -> None:
        width, height = self.swapchain_extent

        if(width <= 0 or height <= 0):
            width = self.fallback_width
            height = self.fallback_height

        self.update_uniforms(width, height)
        self.sprite.draw_rect(0, 0, width, height)
        self.print_text("MXVK Compute Shader", 15, 15, mxvk.Color(255, 255, 255, 255))
        self.frame_count += 1

    def event(self, event: mxvk.Event) -> None:
        if(event.type == mxvk.EVENT_KEY_DOWN):
            if(event.key == mxvk.KEY_ESCAPE):
                self.request_exit()
                return

        if(event.mouse_motion):
            self.mouse_x = event.x
            self.mouse_y = event.y
            return

        if(event.mouse_button_down):
            if(event.button == mxvk.MOUSE_BUTTON_LEFT):
                self.mouse_x = event.x
                self.mouse_y = event.y
                self.mouse_pressed = True
                return

        if(event.mouse_button_up):
            if(event.button == mxvk.MOUSE_BUTTON_LEFT):
                self.mouse_x = event.x
                self.mouse_y = event.y
                self.mouse_pressed = False
                return


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MXVK Python compute shader example")
    parser.add_argument("--filename", type=str, default="inter.comp.spv")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--vsync", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    window = None

    try:
        window = SpriteWindow(args.filename, args.width, args.height, args.fullscreen, args.vsync)
        window.loop()
    except mxvk.MXVKError as exception:
        print(f"mxvk: Exception: {exception}")
        return 1
    finally:
        if(window is not None):
            window.close()

    return 0


if(__name__ == "__main__"):
    raise SystemExit(main())
