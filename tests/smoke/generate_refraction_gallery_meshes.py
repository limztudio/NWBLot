#!/usr/bin/env python3
"""Regenerate deterministic, closed meshes for the refraction smoke gallery.

The torus has its normal axis along +Z: tilt it nearly edge-on about Y to expose
both lobes to a camera looking along +Z. The two sphere shells are disconnected
components of ONE mesh and must be drawn by ONE renderer to exercise same-instance
re-entry. The prism has an equilateral XZ cross-section extruded along Y; oblique
placement stresses exit normals and may produce total internal reflection.

Run without arguments to write assets, or with --check to validate that checked-in
assets exactly match the generator. Only the Python standard library is required.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass, field
import math
from pathlib import Path


Vec3 = tuple[float, float, float]
Triangle = tuple[int, int, int]
ROOT = Path(__file__).resolve().parent / "assets" / "meshes"


def add(a: Vec3, b: Vec3) -> Vec3:
    return tuple(x + y for x, y in zip(a, b))


def sub(a: Vec3, b: Vec3) -> Vec3:
    return tuple(x - y for x, y in zip(a, b))


def mul(a: Vec3, scale: float) -> Vec3:
    return tuple(x * scale for x in a)


def dot(a: Vec3, b: Vec3) -> float:
    return sum(x * y for x, y in zip(a, b))


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def unit(value: Vec3) -> Vec3:
    length = math.sqrt(dot(value, value))
    assert length > 1e-12
    return mul(value, 1.0 / length)


@dataclass
class Mesh:
    positions: list[Vec3] = field(default_factory=list)
    normals: list[Vec3] = field(default_factory=list)
    tangents: list[tuple[float, float, float, float]] = field(default_factory=list)
    uv0: list[tuple[float, float]] = field(default_factory=list)
    vertex_refs: list[tuple[int, int, int, int, int]] = field(default_factory=list)
    indices: list[Triangle] = field(default_factory=list)

    def smooth_vertex(self, position: Vec3, normal: Vec3, tangent: Vec3, uv: tuple[float, float]) -> int:
        ref = len(self.vertex_refs)
        self.vertex_refs.append((len(self.positions), len(self.normals), len(self.tangents), len(self.uv0), 0))
        self.positions.append(position)
        self.normals.append(unit(normal))
        self.tangents.append((*unit(tangent), 1.0))
        self.uv0.append(uv)
        return ref

    def triangle(self, a: int, b: int, c: int) -> None:
        refs = [self.vertex_refs[index] for index in (a, b, c)]
        positions = [self.positions[ref[0]] for ref in refs]
        normal = cross(sub(positions[1], positions[0]), sub(positions[2], positions[0]))
        expected = tuple(sum(self.normals[ref[1]][axis] for ref in refs) for axis in range(3))
        self.indices.append((a, b, c) if dot(normal, expected) > 0.0 else (a, c, b))

    def flat_polygon(self, position_indices: list[int], uvs: list[tuple[float, float]]) -> None:
        points = [self.positions[index] for index in position_indices]
        normal = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
        center = mul(tuple(sum(point[axis] for point in points) for axis in range(3)), 1.0 / len(points))
        # The prism is convex and centered on the origin, so every face normal points away from the origin.
        if dot(normal, center) < 0.0:
            position_indices = list(reversed(position_indices))
            uvs = list(reversed(uvs))
            points = list(reversed(points))
            normal = mul(normal, -1.0)
        normal_index, tangent_index = len(self.normals), len(self.tangents)
        self.normals.append(normal)
        self.tangents.append((*unit(sub(points[1], points[0])), 1.0))
        refs = []
        for position_index, uv in zip(position_indices, uvs):
            refs.append(len(self.vertex_refs))
            self.vertex_refs.append((position_index, normal_index, tangent_index, len(self.uv0), 0))
            self.uv0.append(uv)
        for index in range(1, len(refs) - 1):
            self.triangle(refs[0], refs[index], refs[index + 1])


def torus() -> Mesh:
    mesh = Mesh()
    major_segments, minor_segments = 40, 16
    major_radius, minor_radius = 0.85, 0.32
    for major in range(major_segments):
        theta = math.tau * major / major_segments
        for minor in range(minor_segments):
            phi = math.tau * minor / minor_segments
            radial = major_radius + minor_radius * math.cos(phi)
            mesh.smooth_vertex(
                (radial * math.cos(theta), radial * math.sin(theta), minor_radius * math.sin(phi)),
                (math.cos(phi) * math.cos(theta), math.cos(phi) * math.sin(theta), math.sin(phi)),
                (-math.sin(theta), math.cos(theta), 0.0),
                (major / major_segments, minor / minor_segments),
            )
    for major in range(major_segments):
        for minor in range(minor_segments):
            a = major * minor_segments + minor
            b = ((major + 1) % major_segments) * minor_segments + minor
            c = major * minor_segments + (minor + 1) % minor_segments
            d = ((major + 1) % major_segments) * minor_segments + (minor + 1) % minor_segments
            mesh.triangle(a, b, c)
            mesh.triangle(b, d, c)
    return mesh


def append_sphere(mesh: Mesh, center: Vec3, radius: float) -> None:
    longitude_segments, latitude_segments = 20, 12
    top = mesh.smooth_vertex(add(center, (0.0, radius, 0.0)), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.5, 0.0))
    rings = []
    for latitude in range(1, latitude_segments):
        phi = math.pi * latitude / latitude_segments
        ring = []
        for longitude in range(longitude_segments):
            theta = math.tau * longitude / longitude_segments
            normal = (math.sin(phi) * math.cos(theta), math.cos(phi), math.sin(phi) * math.sin(theta))
            ring.append(mesh.smooth_vertex(
                add(center, mul(normal, radius)), normal, (-math.sin(theta), 0.0, math.cos(theta)),
                (longitude / longitude_segments, latitude / latitude_segments),
            ))
        rings.append(ring)
    bottom = mesh.smooth_vertex(add(center, (0.0, -radius, 0.0)), (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (0.5, 1.0))
    for longitude in range(longitude_segments):
        next_longitude = (longitude + 1) % longitude_segments
        mesh.triangle(top, rings[0][longitude], rings[0][next_longitude])
        for ring, next_ring in zip(rings, rings[1:]):
            mesh.triangle(ring[longitude], next_ring[longitude], ring[next_longitude])
            mesh.triangle(ring[next_longitude], next_ring[longitude], next_ring[next_longitude])
        mesh.triangle(bottom, rings[-1][next_longitude], rings[-1][longitude])


def two_shells() -> Mesh:
    mesh = Mesh()
    centers = [(-0.25, 0.0, -0.9), (0.25, 0.0, 0.9)]
    radius = 0.75
    assert math.dist(*centers) > 2.0 * radius
    for center in centers:
        append_sphere(mesh, center, radius)
    return mesh


def prism() -> Mesh:
    mesh = Mesh()
    half_width, half_height = 0.95, 0.95
    triangle_height = math.sqrt(3.0) * half_width
    cross_section = [(-half_width, -triangle_height / 3.0), (half_width, -triangle_height / 3.0), (0.0, 2.0 * triangle_height / 3.0)]
    mesh.positions = [(x, y, z) for y in (-half_height, half_height) for x, z in cross_section]
    cap_uv = [(0.0, 0.0), (1.0, 0.0), (0.5, 1.0)]
    mesh.flat_polygon([0, 1, 2], cap_uv)
    mesh.flat_polygon([3, 4, 5], cap_uv)
    for index in range(3):
        following = (index + 1) % 3
        mesh.flat_polygon([index, following, following + 3, index + 3], [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)])
    return mesh


def validate(mesh: Mesh, expected_components: int, expected_euler: int, expected_bounds: tuple[Vec3, Vec3]) -> None:
    assert 0 < len(mesh.indices) <= 1500
    for values in (mesh.positions, mesh.normals, mesh.tangents, mesh.uv0):
        assert all(math.isfinite(value) for row in values for value in row)
    assert all(abs(dot(normal, normal) - 1.0) < 1e-12 for normal in mesh.normals)
    for position, normal, tangent, uv, color in mesh.vertex_refs:
        assert 0 <= position < len(mesh.positions) and 0 <= normal < len(mesh.normals)
        assert 0 <= tangent < len(mesh.tangents) and 0 <= uv < len(mesh.uv0) and color == 0
        assert abs(dot(mesh.normals[normal], mesh.tangents[tangent][:3])) < 1e-12
    edges = defaultdict(list)
    neighbors = defaultdict(set)
    position_triangles = []
    for triangle in mesh.indices:
        assert all(0 <= index < len(mesh.vertex_refs) for index in triangle)
        refs = [mesh.vertex_refs[index] for index in triangle]
        indices = tuple(ref[0] for ref in refs)
        points = [mesh.positions[index] for index in indices]
        face_normal = cross(sub(points[1], points[0]), sub(points[2], points[0]))
        assert dot(face_normal, face_normal) > 1e-16, "Degenerate triangle"
        average_normal = tuple(sum(mesh.normals[ref[1]][axis] for ref in refs) for axis in range(3))
        assert dot(face_normal, average_normal) > 1e-10, "Winding disagrees with outward normals"
        position_triangles.append(indices)
        for a, b in zip(indices, indices[1:] + indices[:1]):
            edges[min(a, b), max(a, b)].append((a, b))
            neighbors[a].add(b)
            neighbors[b].add(a)
    assert len(neighbors) == len(mesh.positions), "Unused position"
    assert all(len(pair) == 2 and pair[0] == pair[1][::-1] for pair in edges.values()), "Mesh is not a closed oriented manifold"
    assert len(mesh.positions) - len(edges) + len(mesh.indices) == expected_euler
    components = []
    remaining = set(neighbors)
    while remaining:
        pending, component = [min(remaining)], set()
        while pending:
            current = pending.pop()
            if current not in component:
                component.add(current)
                pending.extend(neighbors[current] - component)
        remaining.difference_update(component)
        components.append(component)
    assert len(components) == expected_components
    for component in components:
        signed_volume = sum(dot(mesh.positions[a], cross(mesh.positions[b], mesh.positions[c])) / 6.0
                            for a, b, c in position_triangles if a in component)
        assert signed_volume > 0.01, "Component winding points inward"
    bounds = tuple(tuple(operation(point[axis] for point in mesh.positions) for axis in range(3)) for operation in (min, max))
    assert all(abs(actual - expected) < 1e-9 for row, expected_row in zip(bounds, expected_bounds) for actual, expected in zip(row, expected_row))


def serialize(mesh: Mesh) -> str:
    def number(value: float | int) -> str:
        if isinstance(value, int):
            return str(value)
        return f"{0.0 if abs(value) < 0.5e-9 else value:.9f}"

    lines = ["// Generated by tests/smoke/generate_refraction_gallery_meshes.py; do not edit by hand.", "mesh asset;", ""]
    for name, rows in (
        ("positions", mesh.positions), ("normals", mesh.normals), ("tangents", mesh.tangents), ("uv0", mesh.uv0),
        ("colors", [(1.0, 1.0, 1.0, 1.0)]), ("vertex_refs", mesh.vertex_refs), ("indices", mesh.indices),
    ):
        lines.append(f"asset.{name} = [")
        lines.extend("    [" + ", ".join(number(value) for value in row) + "]," for row in rows)
        lines.extend(["];", ""])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Validate existing asset text without writing files")
    args = parser.parse_args()
    prism_height = math.sqrt(3.0) * 0.95
    assets = [
        ("refraction_torus.nwb", torus(), 1, 0, ((-1.17, -1.17, -0.32), (1.17, 1.17, 0.32))),
        ("refraction_two_shells.nwb", two_shells(), 2, 4, ((-1.0, -0.75, -1.65), (1.0, 0.75, 1.65))),
        ("refraction_prism.nwb", prism(), 1, 2, ((-0.95, -0.95, -prism_height / 3.0), (0.95, 0.95, 2.0 * prism_height / 3.0))),
    ]
    for filename, mesh, components, euler, bounds in assets:
        validate(mesh, components, euler, bounds)
        path = ROOT / filename
        content = serialize(mesh).replace("\n", "\r\n").encode("utf-8")
        if args.check:
            assert path.read_bytes() == content, f"Stale generated mesh or non-CRLF line endings: {path}"
        else:
            path.write_bytes(content)
        print(f"{filename}: {len(mesh.positions)} positions, {len(mesh.indices)} triangles, {components} closed components; bounds {bounds}")


if __name__ == "__main__":
    main()
