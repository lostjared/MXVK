#!/usr/bin/env python3
"""Open a minimal MXVK window from Python."""

try:
    import mxvk_ext as mxvk
except ImportError as error:
    raise SystemExit(
        "Could not import mxvk_ext. Build with -DPYTHON_MODULE=ON and run with:\n"
        "  PYTHONPATH=build python3 python-examples/window/main.py"
    ) from error


def main() -> None:
    window = mxvk.Window(
        "MXVK Python Window",
        960,
        540,
        fullscreen=False,
        validation=False,
        present_mode=mxvk.PresentModePreference.vsync,
        runtime_mode=mxvk.RuntimeMode.windowed,
    )
    window.set_clear_color(0.06, 0.09, 0.16, 1.0)

    try:
        window.loop()
    finally:
        window.release()


if __name__ == "__main__":
    main()
