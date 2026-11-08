"""Analytic scene geometry for the optical reference."""

from dataclasses import dataclass
from functools import lru_cache
import json
import math
from pathlib import Path
import re
import struct

LIT_E = "e"
LIT_OPTICAL_CSG_UNSPECIFIED_CAP = "optical_csg_unspecified_cap"
LIT_OPTICAL_CSG_CAP = "optical_csg_cap"
LIT_OPTICAL_CSG_CAVITY = "optical_csg_cavity"
LIT_OPTICAL_CSG_OPEN_TAIL = "optical_csg_open_tail"
LIT_NESTED = "nested"
LIT_OPTICAL_TIR = "optical_tir"
LIT_SINGLE = "single"
LIT_OPTICAL_PRIORITY_TIE_B = "optical_priority_tie_b"
LIT_FIRST = "first"
LIT_PRIORITY = "priority"
LIT_SECOND = "second"
LIT_OPTICAL_PRIORITY_A = "optical_priority_a"
LIT_OPTICAL_MIXED = "optical_mixed"
LIT_OPTICAL_NESTED3 = "optical_nested3"
LIT_OPTICAL_OVERFLOW = "optical_overflow"
LIT_UNSPECIFIED = "unspecified"
LIT_UTF_8 = "utf-8"


def half(value):
    return struct.unpack(LIT_E, struct.pack(LIT_E, value))[0]


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, factor):
    return tuple(x * factor for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalized(a):
    return scale(a, 1.0 / math.sqrt(dot(a, a)))


@dataclass(frozen=True)
class Boundary:
    name: str
    ior: float
    transmission: tuple
    planes: tuple = ()
    triangles: tuple = ()
    mode: str = LIT_NESTED
    priority: int = 0
    tie_order: int = 0

    def contains(self, point):
        if self.planes:
            return all(dot(normal, point) < distance - 1e-7 for normal, distance in self.planes)
        # Reference rays start outside the torus behind the camera.
        return False

    def intersect(self, origin, direction):
        if self.planes:
            near, far, near_normal, far_normal = -math.inf, math.inf, None, None
            for normal, distance in self.planes:
                denominator = dot(normal, direction)
                remaining = distance - dot(normal, origin)
                if abs(denominator) < 1e-12:
                    if remaining < 0.0:
                        return None
                    continue
                parameter = remaining / denominator
                if denominator < 0.0 and parameter > near:
                    near, near_normal = parameter, normal
                elif denominator > 0.0 and parameter < far:
                    far, far_normal = parameter, normal
            if near > far:
                return None
            return (near, near_normal) if near > 1e-6 else (far, far_normal) if far > 1e-6 else None
        closest = None
        for a, edge_b, edge_c, normal in self.triangles:
            p = cross(direction, edge_c)
            determinant = dot(edge_b, p)
            if abs(determinant) < 1e-12:
                continue
            relative = add(origin, scale(a, -1.0))
            u = dot(relative, p) / determinant
            if u < -1e-8 or u > 1.0 + 1e-8:
                continue
            q = cross(relative, edge_b)
            v = dot(direction, q) / determinant
            if v < -1e-8 or u + v > 1.0 + 1e-8:
                continue
            distance = dot(edge_c, q) / determinant
            if distance > 1e-6 and (closest is None or distance < closest[0]):
                closest = distance, normal
        return closest


CLEAR = (1.0, 1.0, 1.0)
TINTED = tuple(map(half, (0.55, 0.8, 1.0)))
WARM = tuple(map(half, (1.0, 0.7, 0.45)))


class ReflectionOpticalScene:
    @staticmethod
    def box(name, center_z, thickness, ior=1.5, transmission=TINTED, mode=LIT_NESTED, priority=0, yaw=0.0, tie_order=0, xy_scale=1.0):
        cosine, sine = math.cos(yaw), math.sin(yaw)
        axes = ((cosine, 0.0, -sine), (0.0, 1.0, 0.0), (sine, 0.0, cosine))
        center = (0.0, 1.4, center_z)
        planes = tuple((scale(axis, sign), extent + dot(scale(axis, sign), center)) for axis, extent in zip(axes, (12.0 * xy_scale, 9.0 * xy_scale, thickness * 0.5)) for sign in (-1.0, 1.0))
        return Boundary(name, half(ior), tuple(map(half, transmission)), planes, mode=mode, priority=priority, tie_order=tie_order)

    @staticmethod
    @lru_cache(maxsize=1)
    def authored_torus_triangles():
        source = (Path(__file__).parent / "assets/meshes/refraction_torus.nwb").read_text(encoding=LIT_UTF_8)
        def rows(field):
            text = re.search(r"asset\." + field + r"\s*=\s*(\[.*?\]);", source, re.S).group(1)
            return json.loads(re.sub(r",\s*]", "]", text))
        # Apply scale 2 and X rotation -pi/2 while preserving triangle normals.
        positions = [(2 * x, 1.4 + 2 * z, -9.5 - 2 * y) for x, y, z in rows("positions")]
        references = rows("vertex_refs")
        triangles = []
        for indices in rows("indices"):
            a, b, c = (positions[references[index][0]] for index in indices)
            edge_b, edge_c = add(b, scale(a, -1)), add(c, scale(a, -1))
            triangles.append((a, edge_b, edge_c, normalized(cross(edge_b, edge_c))))
        return tuple(triangles)

    @staticmethod
    @lru_cache(maxsize=32)
    def boundaries(case):
        if case == "optical_reference":
            return ()
        if case in ("optical_unspecified_reference", LIT_OPTICAL_CSG_UNSPECIFIED_CAP):
            return (ReflectionOpticalScene.box("retained_air", -9.5, 1, 1., CLEAR, LIT_UNSPECIFIED),)
        if case in ("optical_csg_reference", LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_CAVITY, LIT_OPTICAL_CSG_OPEN_TAIL):
            return (ReflectionOpticalScene.box("retained", -9.5, 1),)
        if case == LIT_OPTICAL_TIR:
            inverse_root_two = math.sqrt(0.5)
            planes = (((1.0, 0.0, 0.0), 20.0), ((0.0, 0.0, 1.0), 2.0), ((-inverse_root_two, 0.0, -inverse_root_two), 8 * inverse_root_two), ((0.0, 1.0, 0.0), 11.4), ((0.0, -1.0, 0.0), 8.6))
            return (Boundary("prism", half(1.5), CLEAR, planes),)
        if case.startswith("optical_inside"):
            outer = ReflectionOpticalScene.box("outer", -3, 14, 1.5, CLEAR)
            return (outer, ReflectionOpticalScene.box("inner", -3, 10, 1.33, CLEAR, xy_scale=0.7)) if case.endswith(LIT_NESTED) else (outer,)
        if case in ("optical_same_mesh", "optical_disconnected"):
            # Treat re-entry into a disconnected component of the same instance as a new interval.
            return (ReflectionOpticalScene.box("front", -8, 0.5), ReflectionOpticalScene.box("back", -10, 0.5))
        if case == "optical_union_single":
            return (ReflectionOpticalScene.box(LIT_SINGLE, -9, 0.8),)
        if case == "optical_union_same_mesh":
            return (ReflectionOpticalScene.box("front_component", -8.85, 0.5), ReflectionOpticalScene.box("back_component", -9.15, 0.5))
        if case == "optical_coincident_independent":
            return (ReflectionOpticalScene.box("independent_first", -9, 2), ReflectionOpticalScene.box("independent_second", -9, 2))
        if case == "optical_torus":
            return (Boundary("torus", half(1.5), TINTED, triangles=ReflectionOpticalScene.authored_torus_triangles()),)
        if case in ("optical_priority_tie_a", LIT_OPTICAL_PRIORITY_TIE_B):
            reverse = case == LIT_OPTICAL_PRIORITY_TIE_B
            return (ReflectionOpticalScene.box(LIT_FIRST, -8.5, 2, mode=LIT_PRIORITY, priority=10, tie_order=int(reverse)), ReflectionOpticalScene.box(LIT_SECOND, -9.5, 2, 1.33, WARM, LIT_PRIORITY, 10, tie_order=int(not reverse)))
        if case in (LIT_OPTICAL_PRIORITY_A, "optical_priority_b", LIT_OPTICAL_MIXED):
            first_wins = case == LIT_OPTICAL_PRIORITY_A
            return (ReflectionOpticalScene.box(LIT_FIRST, -8.5, 2, mode=LIT_PRIORITY, priority=20 if first_wins else 10), ReflectionOpticalScene.box(LIT_SECOND, -9.5, 2, 1.33, WARM, LIT_NESTED if case == LIT_OPTICAL_MIXED else LIT_PRIORITY, 10 if first_wins else 20))
        if case in ("optical_nested2", LIT_OPTICAL_NESTED3, LIT_OPTICAL_OVERFLOW):
            count = 5 if case == LIT_OPTICAL_OVERFLOW else 3 if case == LIT_OPTICAL_NESTED3 else 2
            return tuple(ReflectionOpticalScene.box(str(index), -9, 4 - 0.6 * index if count == 5 else 3 / (1 << index), 1.5 if index == 0 else 1.33 if index == 1 else 1.1 + 0.02 * (index - 2), TINTED if index == 0 else WARM, xy_scale=1.0 - 0.1 * index) for index in range(count))
        return (ReflectionOpticalScene.box(LIT_SINGLE, -9, 2, transmission=CLEAR if case == "optical_clear" else TINTED, mode=LIT_UNSPECIFIED if case == "optical_unspecified" else LIT_NESTED, yaw=0.2 if case == "optical_tilted" else 0.0),)

