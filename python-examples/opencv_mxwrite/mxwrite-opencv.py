#!/usr/bin/env python3
## @file mxwrite-opencv.py
## @brief Capture OpenCV video through MXVK and encode it with MXWrite.
## @details Uses @c mxvk_ext.Capture for camera or file input, displays frames
## in an MXVK sprite, and writes file input as constant-frame-rate video.
## Camera capture retains monotonic timestamps. The example is intentionally
## split so MXVK owns capture/windowing and MXWrite owns encoding/muxing.
##
## @section mxwrite_opencv_run Running the example
## @code{.sh}
## python3 python-examples/opencv_mxwrite/mxwrite-opencv.py --input input.mp4 --output output.mp4
## python3 python-examples/opencv_mxwrite/mxwrite-opencv.py --camera 0 --width 1280 --height 720
## @endcode
## @section mxwrite_opencv_requirements Requirements
## Requires a CV-enabled @c mxvk_ext module, @c mxwrite_ext, OpenCV, and NumPy.
## The Linux camera path requests V4L2 MJPEG; other platforms use OpenCV's
## default camera backend.
## @section mxwrite_opencv_timing Timing and controls
## File input uses sequential PTS values at the input frame rate for CFR output.
## Camera input preserves monotonic capture timestamps. Escape or Ctrl-C stops
## capture; @c --frames limits the number of submitted frames.

import argparse
import math
import sys
import time
from pathlib import Path

try:
    import cv2
except ImportError as error:
    raise SystemExit("OpenCV is required: python3 -m pip install opencv-python") from error

try:
    import mxwrite_ext
except ImportError as error:
    raise SystemExit("Could not import mxwrite_ext. Build with -DPYTHON_MODULE=ON and set PYTHONPATH to the directory containing the built module.") from error

try:
    import mxvk_ext as mxvk
except ImportError as error:
    raise SystemExit("Could not import mxvk_ext. Build with -DPYTHON_MODULE=ON and set PYTHONPATH to the directory containing the built module.") from error

if not getattr(mxvk, "has_cv", False):
    raise SystemExit("mxvk_ext was built without OpenCV support. Rebuild MXVK with -DCV=ON -DPYTHON_MODULE=ON.")


def parse_arguments():
    parser = argparse.ArgumentParser(description="Capture video from a file or webcam and encode it with MXWrite using timestamps.")

    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="Input video file.")
    source.add_argument("--camera", type=int, help="Webcam device index, for example --camera 0.")

    parser.add_argument("--output", type=Path, default=Path("mxwrite-output.mp4"))
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument("--frames", type=int, default=0, help="Maximum number of frames. 0 means unlimited.")
    parser.add_argument("--codec", default="libx264")
    parser.add_argument("--preset", default="veryfast")
    parser.add_argument("--crf", type=int, default=20)
    parser.add_argument("--normalize-pts", action="store_true")

    return parser.parse_args()


def validate_arguments(args):
    if args.input is not None and not args.input.is_file():
        raise SystemExit(f"Input file does not exist: {args.input}")

    if args.width <= 0 or args.height <= 0:
        raise SystemExit("Width and height must be positive.")

    if args.fps <= 0.0:
        raise SystemExit("FPS must be positive.")

    if args.frames < 0:
        raise SystemExit("Frame count cannot be negative.")

    if not 0 <= args.crf <= 51:
        raise SystemExit("CRF must be between 0 and 51.")


def fourcc_to_string(value):
    value = int(value)

    if value <= 0:
        return "unknown"

    return "".join(chr((value >> (8 * i)) & 0xff) for i in range(4))


def open_file_capture(filename):
    capture = mxvk.Capture()

    if not capture.open(str(filename)):
        raise SystemExit(f"Could not open input video: {filename}")

    return capture


def open_camera_capture(index, width, height, fps):
    if sys.platform.startswith("linux"):
        print("Opening camera with Linux V4L2 backend.")
        capture = mxvk.Capture()

        if not capture.open_camera(index, cv2.CAP_V4L2):
            raise SystemExit(f"Could not open camera {index} using V4L2")

        fourcc = cv2.VideoWriter_fourcc("M", "J", "P", "G")

        capture.set(cv2.CAP_PROP_FOURCC, fourcc)
        capture.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        capture.set(cv2.CAP_PROP_FPS, fps)
    else:
        print("Opening camera with the default OpenCV backend.")
        capture = mxvk.Capture()

        if not capture.open_camera(index):
            raise SystemExit(f"Could not open camera {index}")

        capture.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        capture.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        capture.set(cv2.CAP_PROP_FPS, fps)

    actual_width = int(capture.get(cv2.CAP_PROP_FRAME_WIDTH))
    actual_height = int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT))
    actual_fps = float(capture.get(cv2.CAP_PROP_FPS))
    actual_fourcc = capture.get(cv2.CAP_PROP_FOURCC)

    print("Camera backend: MXVK OpenCV capture")
    print(f"Camera format: {fourcc_to_string(actual_fourcc)}")
    print(f"Camera resolution: {actual_width}x{actual_height}")
    print(f"Camera FPS: {actual_fps:.3f}")

    if actual_width != width or actual_height != height:
        print(f"Warning: requested {width}x{height}, but camera negotiated {actual_width}x{actual_height}.")

    if actual_fps > 0.0 and not math.isclose(actual_fps, fps, rel_tol=0.01, abs_tol=0.1):
        print(f"Warning: requested {fps:.3f} FPS, but camera negotiated {actual_fps:.3f} FPS.")

    return capture


def get_file_timestamp(capture, source_fps, frame_index):
    if hasattr(cv2, "CAP_PROP_PTS"):
        pts = capture.get(cv2.CAP_PROP_PTS)

        if math.isfinite(pts) and pts >= 0.0:
            return pts / source_fps, "PTS"

    position_ms = capture.get(cv2.CAP_PROP_POS_MSEC)

    if math.isfinite(position_ms) and position_ms >= 0.0:
        return position_ms / 1000.0, "POS_MSEC"

    return frame_index / source_fps, "FRAME_INDEX"


def configure_encoder(args):
    encoders = mxwrite_ext.available_video_encoders()
    matching_encoders = [encoder for encoder in encoders if encoder.name == args.codec]

    print(f"MXWrite reported {len(encoders)} video encoder(s).")

    if matching_encoders:
        encoder = matching_encoders[0]
        mode = "hardware" if encoder.hardware else "software"

        print(f"Using {encoder.name} ({encoder.long_name}, {mode}).")

        options = mxwrite_ext.video_encoder_options(encoder.name)

        print(f"The encoder exposes {len(options)} configurable option(s).")

    elif args.codec not in {"auto", "software", "cpu", "x264", "h264", "hevc", "h265", "nvenc"}:
        names = ", ".join(encoder.name for encoder in encoders[:12])
        raise SystemExit(f"Requested encoder '{args.codec}' is unavailable. Available encoders include: {names}")

    encode_options = mxwrite_ext.EncodeOptions()
    encode_options.codec = args.codec
    encode_options.preset = args.preset
    encode_options.crf = args.crf
    encode_options.block_when_full = True

    return encode_options


class CaptureWindow(mxvk.VK_Window):
    def __init__(self, args, capture, is_camera, source_width, source_height, source_fps, output_fps, writer):
        super().__init__("MXWrite Capture", source_width, source_height, False, False, False)
        self.args = args
        self.capture = capture
        self.is_camera = is_camera
        self.source_fps = source_fps
        self.output_fps = output_fps
        self.writer = writer
        self.sprite = self.create_sprite(source_width, source_height)
        self.frame_index = 0
        self.first_timestamp = None
        self.start_time = None
        self.last_pts = None
        self.timestamp_source = None
        self.capture_failed = False
        self.closed = False

    def event(self, event):
        if event.type == mxvk.EVENT_KEY_DOWN and event.key == mxvk.KEY_ESCAPE:
            print("Escape pressed. Stopping capture.")
            self.request_exit()

    def proc(self):
        if self.args.frames > 0 and self.frame_index >= self.args.frames:
            self.request_exit()
            return

        rgba_frame = self.capture.read_rgba()

        if rgba_frame is None:
            if self.is_camera and not self.capture_failed:
                print("Camera capture failed.")
                self.capture_failed = True
            self.request_exit()
            return

        if self.is_camera:
            capture_time = time.monotonic()

            if self.start_time is None:
                self.start_time = capture_time

            timestamp = capture_time - self.start_time
            current_source = "MONOTONIC"
        else:
            timestamp, current_source = get_file_timestamp(self.capture, self.source_fps, self.frame_index)

            if self.first_timestamp is None:
                self.first_timestamp = timestamp

            if self.args.normalize_pts:
                timestamp -= self.first_timestamp

        if self.timestamp_source != current_source:
            self.timestamp_source = current_source
            print(f"Timestamp source: {self.timestamp_source}")

        # File playback is encoded as constant-frame-rate output: every
        # decoded frame occupies exactly one output frame interval. Camera
        # capture keeps its existing monotonic-clock timestamps.
        output_pts = self.frame_index if not self.is_camera else int(round(timestamp * self.output_fps))

        if self.last_pts is not None and output_pts < self.last_pts:
            print(f"Warning: non-monotonic PTS at frame {self.frame_index}: {output_pts} < {self.last_pts}")
            output_pts = self.last_pts

        self.writer.write_at_pts(rgba_frame, output_pts)
        self.sprite.update_texture(rgba_frame, rgba_frame.shape[1], rgba_frame.shape[0], rgba_frame.strides[0])

        width, height = self.swapchain_extent
        self.sprite.draw_rect(0, 0, width, height)
        self.last_pts = output_pts

        if self.frame_index < 10 or self.frame_index % 100 == 0:
            print(f"frame={self.frame_index:6d} time={timestamp:10.6f}s pts={output_pts:8d}")

        self.frame_index += 1

    def close(self):
        if self.closed:
            return

        self.closed = True
        self.wait_idle()
        self.sprite = None
        self.writer.close()
        self.capture.close()
        self.release()


def main():
    args = parse_arguments()
    validate_arguments(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    is_camera = args.camera is not None
    capture = open_camera_capture(args.camera, args.width, args.height, args.fps) if is_camera else open_file_capture(args.input)
    window = None
    writer = None

    try:
        source_width = int(capture.get(cv2.CAP_PROP_FRAME_WIDTH))
        source_height = int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT))
        source_fps = float(capture.get(cv2.CAP_PROP_FPS))

        if source_width <= 0 or source_height <= 0:
            raise SystemExit(f"Invalid capture resolution: {source_width}x{source_height}")

        if not math.isfinite(source_fps) or source_fps <= 0.0:
            source_fps = args.fps

        output_fps = args.fps if is_camera else source_fps

        print()
        print("Backend: MXVK OpenCV capture")
        print(f"Resolution: {source_width}x{source_height}")
        print(f"Capture FPS: {source_fps:.6f}")
        print(f"Output time base: 1/{output_fps:.6f}")
        print(f"{'Camera: ' + str(args.camera) if is_camera else 'Input: ' + str(args.input)}")
        print("Press Escape to stop capture.")

        encode_options = configure_encoder(args)
        writer = mxwrite_ext.Writer()

        if not writer.open_ts(str(args.output), source_width, source_height, float(output_fps), encode_options):
            raise SystemExit(f"MXWrite could not open {args.output}")

        window = CaptureWindow(args, capture, is_camera, source_width, source_height, source_fps, output_fps, writer)

        try:
            window.loop()
        except KeyboardInterrupt:
            print("\nCapture stopped.")

        window.close()
        frame_count = writer.get_frame_count()
        duration = writer.get_duration()
        byte_count = writer.get_bytes_written()
        file_size = args.output.stat().st_size if args.output.is_file() else 0

        if file_size == 0:
            raise SystemExit(f"MXWrite did not create a non-empty file at {args.output}.")

        print()
        print(f"PASS: wrote {frame_count} frames, {duration:.3f}s, {file_size} filesystem bytes to {args.output}")
        print(f"MXWrite's muxer byte counter reported {byte_count} bytes.")

        if window.last_pts is not None:
            print(f"Last video PTS: {window.last_pts} ({window.last_pts / output_fps:.6f}s)")
    finally:
        if window is not None:
            window.close()
        else:
            if writer is not None:
                writer.close()
            capture.close()


if __name__ == "__main__":
    main()
