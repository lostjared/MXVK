#!/usr/bin/env python3

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from benchmark_graph import (
    BACKGROUND_COLOR,
    EIGEN_COLOR,
    EIGEN_TIMES_MS as PREVIOUS_EIGEN_TIMES_MS,
    FRAME_COUNT,
    GRID_COLOR,
    MUTED_TEXT_COLOR,
    NATIVE_COLOR,
    NATIVE_TIMES_MS as PREVIOUS_NATIVE_TIMES_MS,
    PANEL_COLOR,
    TEXT_COLOR,
    VULKAN_COLOR,
    VULKAN_TIMES_MS as PREVIOUS_VULKAN_TIMES_MS,
)


NATIVE_TIMES_MS = np.array(
    [4466.856635, 4471.570342, 4473.393354, 4475.139095, 4472.841865]
)
EIGEN_TIMES_MS = np.array(
    [4308.499611, 4332.886647, 4330.142051, 4330.850781, 4322.548658]
)
VULKAN_TIMES_MS = np.array(
    [217.024511, 217.555523, 218.379073, 218.838467, 215.628848]
)

POINT_COLORS = [TEXT_COLOR, "#52D6FF", "#92FFE8"]


def add_bar_label(axis: plt.Axes, value: float, y_position: float) -> None:
    axis.text(
        value + 35,
        y_position,
        f"{value:,.1f} ms",
        va="center",
        color=TEXT_COLOR,
        fontsize=17,
        fontweight="bold",
    )


def main() -> None:
    samples = [NATIVE_TIMES_MS, EIGEN_TIMES_MS, VULKAN_TIMES_MS]
    previous_samples = [
        PREVIOUS_NATIVE_TIMES_MS,
        PREVIOUS_EIGEN_TIMES_MS,
        PREVIOUS_VULKAN_TIMES_MS,
    ]
    means = np.array([values.mean() for values in samples])
    previous_means = np.array([values.mean() for values in previous_samples])
    fps = FRAME_COUNT / (means / 1000.0)
    scale_cost = (means - previous_means) / previous_means * 100.0

    eigen_reduction = (means[0] - means[1]) / means[0] * 100.0
    vulkan_reduction = (means[0] - means[2]) / means[0] * 100.0
    vulkan_speedup = means[0] / means[2]

    figure = plt.figure(figsize=(12, 6.75), dpi=100, facecolor=BACKGROUND_COLOR)
    axis = figure.add_axes((0.14, 0.24, 0.77, 0.46), facecolor=PANEL_COLOR)

    figure.text(
        0.07,
        0.91,
        f"Pixel-heavy sphere: Vulkan is {vulkan_speedup:.1f}× faster",
        color=TEXT_COLOR,
        fontsize=28,
        fontweight="bold",
    )
    figure.text(
        0.07,
        0.845,
        "Scale 2.0 • 65,024 triangles • 320×180 framebuffer • 600 frames • 5-run average",
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

    axis.set_xlim(0, 5100)
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
        f"10× scale cost: Native {scale_cost[0]:+.1f}% • Eigen {scale_cost[1]:+.1f}% • Vulkan {scale_cost[2]:+.1f}%",
        color=MUTED_TEXT_COLOR,
        fontsize=13,
    )
    figure.text(
        0.93,
        0.055,
        f"Eigen saves {eigen_reduction:.1f}% • Vulkan saves {vulkan_reduction:.1f}%",
        color=VULKAN_COLOR,
        fontsize=13,
        fontweight="bold",
        ha="right",
    )

    output_path = Path(__file__).with_name(
        "benchmark_obj_backends_scale2_pixel_heavy.png"
    )
    figure.savefig(output_path, dpi=160, facecolor=figure.get_facecolor())
    plt.close(figure)

    print(f"Native mean: {means[0]:.3f} ms ({fps[0]:.1f} FPS)")
    print(f"Eigen mean: {means[1]:.3f} ms ({fps[1]:.1f} FPS)")
    print(f"Vulkan mean: {means[2]:.3f} ms ({fps[2]:.1f} FPS)")
    print(f"Eigen vs native: {eigen_reduction:.3f}% less time")
    print(
        f"Vulkan vs native: {vulkan_reduction:.3f}% less time "
        f"({vulkan_speedup:.3f}x faster)"
    )
    print(
        "Scale 0.2 to 2.0 timing change: "
        f"native {scale_cost[0]:+.3f}%, "
        f"Eigen {scale_cost[1]:+.3f}%, "
        f"Vulkan {scale_cost[2]:+.3f}%"
    )
    print(output_path)


if __name__ == "__main__":
    main()
