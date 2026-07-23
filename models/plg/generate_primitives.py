#!/usr/bin/env python3
"""Generate the bundled PLG primitive meshes."""

from __future__ import annotations

from dataclasses import dataclass
from itertools import combinations
from math import atan2, cos, pi, sin, sqrt
from pathlib import Path


@dataclass
class Mesh:
    """A triangulated mesh with one UV coordinate per position."""

    vertices: list[tuple[float, float, float, float, float]]
    triangles: list[tuple[int, int, int]]


def uv_for_position(x: float, y: float, z: float) -> tuple[float, float]:
    radius = sqrt(x * x + y * y + z * z)
    if radius == 0.0:
        return 0.5, 0.5
    return 0.5 + atan2(z, x) / (2.0 * pi), 0.5 - asin_clamped(y / radius) / pi


def asin_clamped(value: float) -> float:
    from math import asin

    return asin(max(-1.0, min(1.0, value)))


def with_spherical_uv(points: list[tuple[float, float, float]]) -> list[tuple[float, float, float, float, float]]:
    return [(*point, *uv_for_position(*point)) for point in points]


def orient_outward(mesh: Mesh) -> Mesh:
    """Orient triangles away from the origin for a centered convex mesh."""

    oriented: list[tuple[int, int, int]] = []
    for first, second, third in mesh.triangles:
        a = mesh.vertices[first]
        b = mesh.vertices[second]
        c = mesh.vertices[third]
        ab = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        ac = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        normal = (
            ab[1] * ac[2] - ab[2] * ac[1],
            ab[2] * ac[0] - ab[0] * ac[2],
            ab[0] * ac[1] - ab[1] * ac[0],
        )
        center = (
            (a[0] + b[0] + c[0]) / 3.0,
            (a[1] + b[1] + c[1]) / 3.0,
            (a[2] + b[2] + c[2]) / 3.0,
        )
        if sum(component * position for component, position in zip(normal, center)) < 0.0:
            second, third = third, second
        oriented.append((first, second, third))
    mesh.triangles = oriented
    return mesh


def cube() -> Mesh:
    points = [
        (-0.5, -0.5, -0.5),
        (0.5, -0.5, -0.5),
        (0.5, 0.5, -0.5),
        (-0.5, 0.5, -0.5),
        (-0.5, -0.5, 0.5),
        (0.5, -0.5, 0.5),
        (0.5, 0.5, 0.5),
        (-0.5, 0.5, 0.5),
    ]
    triangles = [
        (0, 1, 2), (0, 2, 3),
        (4, 6, 5), (4, 7, 6),
        (0, 4, 5), (0, 5, 1),
        (3, 2, 6), (3, 6, 7),
        (0, 3, 7), (0, 7, 4),
        (1, 5, 6), (1, 6, 2),
    ]
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def square_pyramid() -> Mesh:
    points = [
        (-0.5, -0.5, -0.5),
        (0.5, -0.5, -0.5),
        (0.5, -0.5, 0.5),
        (-0.5, -0.5, 0.5),
        (0.0, 0.5, 0.0),
    ]
    triangles = [(0, 2, 1), (0, 3, 2), (0, 1, 4), (1, 2, 4), (2, 3, 4), (3, 0, 4)]
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def tetrahedron() -> Mesh:
    scale = 1.0 / sqrt(3.0)
    points = [
        (scale, scale, scale),
        (-scale, -scale, scale),
        (-scale, scale, -scale),
        (scale, -scale, -scale),
    ]
    return orient_outward(Mesh(with_spherical_uv(points), [(0, 1, 2), (0, 3, 1), (0, 2, 3), (1, 3, 2)]))


def octahedron() -> Mesh:
    points = [(1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)]
    triangles = [(2, 0, 4), (2, 4, 1), (2, 1, 5), (2, 5, 0), (3, 4, 0), (3, 1, 4), (3, 5, 1), (3, 0, 5)]
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def icosahedron() -> Mesh:
    golden = (1.0 + sqrt(5.0)) / 2.0
    raw_points = [
        (-1.0, golden, 0.0), (1.0, golden, 0.0), (-1.0, -golden, 0.0), (1.0, -golden, 0.0),
        (0.0, -1.0, golden), (0.0, 1.0, golden), (0.0, -1.0, -golden), (0.0, 1.0, -golden),
        (golden, 0.0, -1.0), (golden, 0.0, 1.0), (-golden, 0.0, -1.0), (-golden, 0.0, 1.0),
    ]
    length = sqrt(1.0 + golden * golden)
    points = [(x / length, y / length, z / length) for x, y, z in raw_points]
    triangles: list[tuple[int, int, int]] = []
    for indices in combinations(range(len(points)), 3):
        a, b, c = (points[index] for index in indices)
        normal = (
            (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
            (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
            (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
        )
        distances = [
            normal[0] * (point[0] - a[0]) + normal[1] * (point[1] - a[1]) + normal[2] * (point[2] - a[2])
            for point in points
        ]
        if all(distance >= -1.0e-7 for distance in distances) or all(distance <= 1.0e-7 for distance in distances):
            triangles.append(indices)
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def triangular_prism() -> Mesh:
    height = sqrt(3.0) / 4.0
    points = [
        (-0.5, -height, -0.5), (0.5, -height, -0.5), (0.0, height, -0.5),
        (-0.5, -height, 0.5), (0.5, -height, 0.5), (0.0, height, 0.5),
    ]
    triangles = [(0, 2, 1), (3, 4, 5), (0, 1, 4), (0, 4, 3), (1, 2, 5), (1, 5, 4), (2, 0, 3), (2, 3, 5)]
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def cylinder(segments: int = 24) -> Mesh:
    points: list[tuple[float, float, float]] = []
    for y in (-0.5, 0.5):
        points.extend((0.5 * cos(2.0 * pi * index / segments), y, 0.5 * sin(2.0 * pi * index / segments)) for index in range(segments))
    points.extend([(0.0, -0.5, 0.0), (0.0, 0.5, 0.0)])
    triangles: list[tuple[int, int, int]] = []
    for index in range(segments):
        following = (index + 1) % segments
        triangles.extend([
            (index, following, segments + following),
            (index, segments + following, segments + index),
            (2 * segments, following, index),
            (2 * segments + 1, segments + index, segments + following),
        ])
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def cone(segments: int = 24) -> Mesh:
    points = [(0.5 * cos(2.0 * pi * index / segments), -0.5, 0.5 * sin(2.0 * pi * index / segments)) for index in range(segments)]
    points.extend([(0.0, 0.5, 0.0), (0.0, -0.5, 0.0)])
    triangles: list[tuple[int, int, int]] = []
    for index in range(segments):
        following = (index + 1) % segments
        triangles.extend([(index, following, segments), (segments + 1, following, index)])
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def surface_of_revolution(profile: list[tuple[float, float]], segments: int) -> Mesh:
    """Build a convex Y-axis surface from (radius, y) rings, including point ends."""

    points: list[tuple[float, float, float]] = [(0.0, profile[0][1], 0.0)]
    ring_indices: list[list[int]] = []
    for radius, y in profile[1:-1]:
        ring = []
        for index in range(segments):
            angle = 2.0 * pi * index / segments
            ring.append(len(points))
            points.append((radius * cos(angle), y, radius * sin(angle)))
        ring_indices.append(ring)
    bottom_index = len(points)
    points.append((0.0, profile[-1][1], 0.0))

    triangles: list[tuple[int, int, int]] = []
    first_ring = ring_indices[0]
    for index in range(segments):
        triangles.append((0, first_ring[index], first_ring[(index + 1) % segments]))
    for first_ring, second_ring in zip(ring_indices, ring_indices[1:]):
        for index in range(segments):
            following = (index + 1) % segments
            triangles.extend([
                (first_ring[index], second_ring[index], second_ring[following]),
                (first_ring[index], second_ring[following], first_ring[following]),
            ])
    last_ring = ring_indices[-1]
    for index in range(segments):
        triangles.append((last_ring[index], bottom_index, last_ring[(index + 1) % segments]))
    return orient_outward(Mesh(with_spherical_uv(points), triangles))


def uv_sphere(segments: int = 24, rings: int = 12) -> Mesh:
    profile = [(0.0, 1.0)]
    for ring in range(1, rings):
        latitude = pi * ring / rings
        profile.append((sin(latitude), cos(latitude)))
    profile.append((0.0, -1.0))
    return surface_of_revolution(profile, segments)


def capsule(segments: int = 24, hemisphere_rings: int = 5) -> Mesh:
    profile = [(0.0, 1.0)]
    for ring in range(1, hemisphere_rings + 1):
        angle = (pi / 2.0) * ring / hemisphere_rings
        profile.append((0.5 * sin(angle), 0.5 + 0.5 * cos(angle)))
    profile.append((0.5, -0.5))
    for ring in range(hemisphere_rings - 1, 0, -1):
        angle = (pi / 2.0) * ring / hemisphere_rings
        profile.append((0.5 * sin(angle), -0.5 - 0.5 * cos(angle)))
    profile.append((0.0, -1.0))
    return surface_of_revolution(profile, segments)


def torus(major_segments: int = 24, minor_segments: int = 12) -> Mesh:
    vertices: list[tuple[float, float, float, float, float]] = []
    for major_index in range(major_segments + 1):
        u = major_index / major_segments
        major_angle = 2.0 * pi * u
        for minor_index in range(minor_segments + 1):
            v = minor_index / minor_segments
            minor_angle = 2.0 * pi * v
            radius = 0.65 + 0.25 * cos(minor_angle)
            vertices.append((radius * cos(major_angle), 0.25 * sin(minor_angle), radius * sin(major_angle), u, v))

    stride = minor_segments + 1
    triangles: list[tuple[int, int, int]] = []
    for major_index in range(major_segments):
        for minor_index in range(minor_segments):
            first = major_index * stride + minor_index
            second = (major_index + 1) * stride + minor_index
            third = second + 1
            fourth = first + 1
            triangles.extend([(first, third, second), (first, fourth, third)])
    return Mesh(vertices, triangles)


def write_mesh(path: Path, name: str, mesh: Mesh, description: str) -> None:
    if any(len(set(triangle)) != 3 for triangle in mesh.triangles):
        raise ValueError(f"{name} contains a degenerate indexed triangle")
    lines = [
        f"# {description}",
        "# Coordinates are centered at the origin; polygon state 0x0003 marks active, visible triangles.",
        f"{name} {len(mesh.vertices)} {len(mesh.triangles)}",
    ]
    lines.extend(f"{x:.6f} {y:.6f} {z:.6f} {u:.6f} {v:.6f}" for x, y, z, u, v in mesh.vertices)
    lines.extend(f"0x0003 3 {first} {second} {third}" for first, second, third in mesh.triangles)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    output_directory = Path(__file__).resolve().parent
    primitives = {
        "cube": (cube(), "Unit cube with shared corner vertices."),
        "pyramid": (square_pyramid(), "Square-based unit pyramid."),
        "tetrahedron": (tetrahedron(), "Regular tetrahedron with unit-radius corners."),
        "octahedron": (octahedron(), "Regular octahedron with unit-radius corners."),
        "icosahedron": (icosahedron(), "Regular icosahedron with unit-radius corners."),
        "triangular_prism": (triangular_prism(), "Triangular prism with unit depth."),
        "cylinder": (cylinder(), "Unit-height cylinder with 24 radial segments."),
        "cone": (cone(), "Unit-height cone with 24 radial segments."),
        "sphere": (uv_sphere(), "Unit-radius UV sphere with 24 segments and 12 latitude bands."),
        "capsule": (capsule(), "Y-axis capsule with radius 0.5 and total height 2."),
        "torus": (torus(), "Y-axis torus with major radius 0.65 and tube radius 0.25."),
    }
    for name, (mesh, description) in primitives.items():
        write_mesh(output_directory / f"{name}.plg", name, mesh, description)


if __name__ == "__main__":
    main()
