#!/usr/bin/env python3

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


NATIVE_TIMES_MS = np.array([1525.164345, 1531.177450, 1527.911411, 1514.609719, 1493.912319])
EIGEN_TIMES_MS = np.array([1326.185721, 1311.472198, 1331.576237, 1321.299109, 1304.143619])
FRAME_COUNT = 600

BACKGROUND_COLOR = "#071525"
PANEL_COLOR = "#0D2238"
TEXT_COLOR = "#F5F9FF"
MUTED_TEXT_COLOR = "#A9C0D5"
GRID_COLOR = "#294158"
NATIVE_COLOR = "#7D93A8"
EIGEN_COLOR = "#1185FE"
ACCENT_COLOR = "#52D6FF"


def add_bar_label(axis, value: float, y_position: float) -> None:
    axis.text(
        value + 18,
        y_position,
        f"{value:,.1f} ms",
        va="center",
        color=TEXT_COLOR,
        fontsize=18,
        fontweight="bold",
    )


def main() -> None:
    native_mean = float(NATIVE_TIMES_MS.mean())
    eigen_mean = float(EIGEN_TIMES_MS.mean())
    time_reduction = (native_mean - eigen_mean) / native_mean * 100.0
    native_fps = FRAME_COUNT / (native_mean / 1000.0)
    eigen_fps = FRAME_COUNT / (eigen_mean / 1000.0)

    figure = plt.figure(figsize=(12, 6.75), dpi=100, facecolor=BACKGROUND_COLOR)
    axis = figure.add_axes((0.09, 0.22, 0.82, 0.48), facecolor=PANEL_COLOR)

    figure.text(
        0.07,
        0.91,
        "Eigen cuts MXVK geometry time by 13.1%",
        color=TEXT_COLOR,
        fontsize=29,
        fontweight="bold",
    )
    figure.text(
        0.07,
        0.845,
        "Heavy sphere • 32,514 vertices • 65,024 triangles • 600 frames",
        color=MUTED_TEXT_COLOR,
        fontsize=15,
    )

    means = [native_mean, eigen_mean]
    y_positions = [1, 0]
    axis.barh(
        y_positions,
        means,
        height=0.48,
        color=[NATIVE_COLOR, EIGEN_COLOR],
        edgecolor="none",
        zorder=2,
    )

    point_offsets = np.linspace(-0.14, 0.14, len(NATIVE_TIMES_MS))
    axis.scatter(
        NATIVE_TIMES_MS,
        1 + point_offsets,
        s=42,
        color=TEXT_COLOR,
        edgecolor=BACKGROUND_COLOR,
        linewidth=1.2,
        zorder=3,
    )
    axis.scatter(
        EIGEN_TIMES_MS,
        point_offsets,
        s=42,
        color=ACCENT_COLOR,
        edgecolor=BACKGROUND_COLOR,
        linewidth=1.2,
        zorder=3,
    )

    add_bar_label(axis, native_mean, 1)
    add_bar_label(axis, eigen_mean, 0)

    axis.set_xlim(0, 1800)
    axis.set_ylim(-0.55, 1.55)
    axis.set_yticks(y_positions, ["Native", "Eigen"])
    axis.set_xlabel("Average time for 600 frames (lower is better)", color=MUTED_TEXT_COLOR, fontsize=13, labelpad=12)
    axis.tick_params(axis="x", colors=MUTED_TEXT_COLOR, labelsize=11)
    axis.tick_params(axis="y", colors=TEXT_COLOR, labelsize=17, length=0, pad=12)
    axis.xaxis.grid(True, color=GRID_COLOR, linewidth=1, alpha=0.65)
    axis.set_axisbelow(True)
    for spine in axis.spines.values():
        spine.set_visible(False)

    figure.text(0.07, 0.10, f"Native  {native_fps:.1f} FPS", color=MUTED_TEXT_COLOR, fontsize=14)
    figure.text(0.30, 0.10, f"Eigen  {eigen_fps:.1f} FPS", color=ACCENT_COLOR, fontsize=14, fontweight="bold")
    figure.text(
        0.93,
        0.10,
        f"{time_reduction:.1f}% less time  •  5-run average  •  RTX 2070",
        color=MUTED_TEXT_COLOR,
        fontsize=13,
        ha="right",
    )

    output_path = Path(__file__).with_name("benchmark_native_vs_eigen.png")
    figure.savefig(output_path, dpi=160, facecolor=figure.get_facecolor())
    plt.close(figure)
    print(output_path)


if __name__ == "__main__":
    main()
