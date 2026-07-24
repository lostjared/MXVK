#!/usr/bin/env python3

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


NATIVE_TIMES_MS = np.array(
    [3214.698109, 3232.651315, 3243.059780, 3197.778456, 3248.197693]
)
EIGEN_TIMES_MS = np.array(
    [3175.934356, 3108.612292, 3148.424937, 3155.347637, 3161.096880]
)
VULKAN_TIMES_MS = np.array(
    [215.720986, 214.006995, 220.344958, 224.275982, 216.000173]
)
FRAME_COUNT = 600

BACKGROUND_COLOR = "#071525"
PANEL_COLOR = "#0D2238"
TEXT_COLOR = "#F5F9FF"
MUTED_TEXT_COLOR = "#A9C0D5"
GRID_COLOR = "#294158"
NATIVE_COLOR = "#7D93A8"
EIGEN_COLOR = "#1185FE"
VULKAN_COLOR = "#00C9A7"
POINT_COLORS = [TEXT_COLOR, "#52D6FF", "#92FFE8"]


def add_bar_label(axis: plt.Axes, value: float, y_position: float) -> None:
    axis.text(
        value + 20,
        y_position,
        f"{value:,.1f} ms",
        va="center",
        color=TEXT_COLOR,
        fontsize=17,
        fontweight="bold",
    )


def main() -> None:
    samples = [NATIVE_TIMES_MS, EIGEN_TIMES_MS, VULKAN_TIMES_MS]
    means = np.array([values.mean() for values in samples])
    fps = FRAME_COUNT / (means / 1000.0)

    eigen_difference = (means[1] - means[0]) / means[0] * 100.0
    vulkan_reduction = (means[0] - means[2]) / means[0] * 100.0
    vulkan_speedup = means[0] / means[2]

    figure = plt.figure(figsize=(12, 6.75), dpi=100, facecolor=BACKGROUND_COLOR)
    axis = figure.add_axes((0.14, 0.24, 0.77, 0.46), facecolor=PANEL_COLOR)

    figure.text(
        0.07,
        0.91,
        f"Vulkan renders the heavy-sphere OBJ {vulkan_speedup:.1f}× faster",
        color=TEXT_COLOR,
        fontsize=28,
        fontweight="bold",
    )
    figure.text(
        0.07,
        0.845,
        "32,514 indexed vertices • 65,024 triangles • 600 frames • 5 runs per backend",
        color=MUTED_TEXT_COLOR,
        fontsize=15,
    )

    y_positions = [2, 1, 0]
    axis.barh(
        y_positions,
        means,
        height=0.48,
        color=[NATIVE_COLOR, EIGEN_COLOR, VULKAN_COLOR],
        edgecolor="none",
        zorder=2,
    )

    point_offsets = np.linspace(-0.14, 0.14, len(NATIVE_TIMES_MS))
    for values, y_position, point_color in zip(
        samples, y_positions, POINT_COLORS, strict=True
    ):
        axis.scatter(
            values,
            y_position + point_offsets,
            s=42,
            color=point_color,
            edgecolor=BACKGROUND_COLOR,
            linewidth=1.2,
            zorder=3,
        )

    for mean, y_position in zip(means, y_positions, strict=True):
        add_bar_label(axis, float(mean), y_position)

    axis.set_xlim(0, 3700)
    axis.set_ylim(-0.55, 2.55)
    axis.set_yticks(y_positions, ["Native CPU", "Eigen CPU", "Vulkan GPU"])
    axis.set_xlabel(
        "Average time for 600 frames (lower is better)",
        color=MUTED_TEXT_COLOR,
        fontsize=13,
        labelpad=12,
    )
    axis.tick_params(axis="x", colors=MUTED_TEXT_COLOR, labelsize=11)
    axis.tick_params(axis="y", colors=TEXT_COLOR, labelsize=16, length=0, pad=12)
    axis.xaxis.grid(True, color=GRID_COLOR, linewidth=1, alpha=0.65)
    axis.set_axisbelow(True)
    for spine in axis.spines.values():
        spine.set_visible(False)

    figure.text(
        0.07,
        0.11,
        f"Native {fps[0]:,.0f} FPS  •  Eigen {fps[1]:,.0f} FPS  •  Vulkan {fps[2]:,.0f} FPS",
        color=TEXT_COLOR,
        fontsize=14,
        fontweight="bold",
    )
    figure.text(
        0.07,
        0.055,
        f"Eigen vs native: {eigen_difference:+.1f}% time  •  Vulkan vs native: {vulkan_reduction:.1f}% less time",
        color=MUTED_TEXT_COLOR,
        fontsize=13,
    )
    figure.text(
        0.93,
        0.055,
        "RTX 2070",
        color=VULKAN_COLOR,
        fontsize=13,
        fontweight="bold",
        ha="right",
    )

    output_path = Path(__file__).with_name("benchmark_obj_backends.png")
    figure.savefig(output_path, dpi=160, facecolor=figure.get_facecolor())
    plt.close(figure)

    print(f"Native mean: {means[0]:.3f} ms ({fps[0]:.1f} FPS)")
    print(f"Eigen mean: {means[1]:.3f} ms ({fps[1]:.1f} FPS)")
    print(f"Vulkan mean: {means[2]:.3f} ms ({fps[2]:.1f} FPS)")
    print(f"Eigen vs native: {eigen_difference:+.3f}% time")
    print(
        f"Vulkan vs native: {vulkan_reduction:.3f}% less time "
        f"({vulkan_speedup:.3f}x faster)"
    )
    print(output_path)


if __name__ == "__main__":
    main()
