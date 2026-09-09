#!/usr/bin/env python3
"""Mesh-derived exterior escape rays for the caustic sphere's smooth environment reflection."""

from dataclasses import dataclass
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import re

from reflection_optical_reference import add, cross, dot, half, normalized, scale


CAMERA = (0.0, 0.85, -2.2)
ENVIRONMENT = (0.6, 0.7, 1.0)
SHOULDER = 0.65
IOR = 1.5
MESH_PATH = Path(__file__).parent / "assets/meshes/caustic_sphere.nwb"


@dataclass(frozen=True)
class Triangle:
    a: tuple
    edge_b: tuple
    edge_c: tuple
    normal: tuple
    shading_normals: tuple
    minimum: tuple
    maximum: tuple

    def intersect(self, origin, direction):
        p = cross(direction, self.edge_c)
        determinant = dot(self.edge_b, p)
        if abs(determinant) < 1e-12:
            return None
        relative = add(origin, scale(self.a, -1))
        u = dot(relative, p) / determinant
        if u < -1e-8 or u > 1 + 1e-8:
            return None
        q = cross(relative, self.edge_b)
        v = dot(direction, q) / determinant
        if v < -1e-8 or u + v > 1 + 1e-8:
            return None
        distance = dot(self.edge_c, q) / determinant
        return (distance, u, v, self) if distance > 1e-7 else None


class MeshNode:
    """CPU median BVH only accelerates independent geometric queries; it has no winding/bootstrap logic."""

    def __init__(self, triangles):
        self.minimum = tuple(min(triangle.minimum[axis] for triangle in triangles) for axis in range(3))
        self.maximum = tuple(max(triangle.maximum[axis] for triangle in triangles) for axis in range(3))
        self.triangles, self.children = (), ()
        if len(triangles) <= 8:
            self.triangles = tuple(triangles)
        else:
            axis = max(range(3), key=lambda index: self.maximum[index] - self.minimum[index])
            ordered = sorted(triangles, key=lambda triangle: triangle.minimum[axis] + triangle.maximum[axis])
            middle = len(ordered) // 2
            self.children = MeshNode(ordered[:middle]), MeshNode(ordered[middle:])

    def intersect(self, origin, direction, closest=math.inf):
        near, far = 0.0, closest
        for axis in range(3):
            if abs(direction[axis]) < 1e-14:
                if origin[axis] < self.minimum[axis] - 1e-9 or origin[axis] > self.maximum[axis] + 1e-9:
                    return None
                continue
            a = (self.minimum[axis] - origin[axis] - 1e-9) / direction[axis]
            b = (self.maximum[axis] - origin[axis] + 1e-9) / direction[axis]
            near, far = max(near, min(a, b)), min(far, max(a, b))
            if far < near:
                return None
        result = None
        for triangle in self.triangles:
            hit = triangle.intersect(origin, direction)
            if hit is not None and hit[0] < closest:
                result, closest = hit, hit[0]
        for child in self.children:
            hit = child.intersect(origin, direction, closest)
            if hit is not None and hit[0] < closest:
                result, closest = hit, hit[0]
        return result


@lru_cache(maxsize=1)
def sphere_mesh():
    source = MESH_PATH.read_text(encoding="utf-8")
    def rows(field):
        text = re.search(r"asset\." + field + r"\s*=\s*(\[.*?\]);", source, re.S).group(1)
        return json.loads(re.sub(r",\s*]", "]", text))
    positions = [(x * 0.7, y * 0.7 + 0.85, z * 0.7) for x, y, z in rows("positions")]
    references, normals = rows("vertex_refs"), rows("normals")
    triangles = []
    for indices in rows("indices"):
        vertices = tuple(positions[references[index][0]] for index in indices)
        a, b, c = vertices
        edge_b, edge_c = add(b, scale(a, -1)), add(c, scale(a, -1))
        triangles.append(Triangle(a, edge_b, edge_c, normalized(cross(edge_b, edge_c)),
            tuple(tuple(normals[references[index][1]]) for index in indices),
            tuple(min(vertex[axis] for vertex in vertices) for axis in range(3)),
            tuple(max(vertex[axis] for vertex in vertices) for axis in range(3))))
    return MeshNode(triangles)


def hits_ground(origin, direction):
    if abs(direction[1]) < 1e-12:
        return False
    distance = (-0.08 - origin[1]) / direction[1]
    if distance <= 0:
        return False
    point = add(origin, scale(direction, distance))
    # Enlarge the authored receiver by 1cm to exclude boundary uncertainty conservatively.
    return abs(point[0]) <= 1.76 and abs(point[2] - 0.08) <= 1.56


@lru_cache(maxsize=4)
def exterior_samples(width=1280, height=900):
    """Regular camera grid; selection depends on mesh geometry, never on which captured pixels are dark."""
    mesh = sphere_mesh()
    focal = height / (2 * math.tan(math.pi / 6))
    silhouette = focal * 0.7 / math.sqrt(2.2 ** 2 - 0.7 ** 2)
    step = max(1, round(height / 225))
    start_x, end_x = math.ceil(width / 2 - silhouette), math.floor(width / 2 + silhouette)
    start_y, end_y = math.ceil(height / 2 - silhouette), math.floor(height / 2 + silhouette)
    selected = []
    for y in range(start_y, end_y + 1, step):
        for x in range(start_x, end_x + 1, step):
            # A 7% silhouette inset and barycentric margin reject grazing rasterization/triangle-edge ambiguity.
            if (x + 0.5 - width / 2) ** 2 + (y + 0.5 - height / 2) ** 2 > (silhouette * 0.93) ** 2:
                continue
            direction = normalized(((x + 0.5 - width / 2) / focal, -(y + 0.5 - height / 2) / focal, 1.0))
            hit = mesh.intersect(CAMERA, direction)
            if hit is None:
                continue
            distance, u, v, triangle = hit
            if min(u, v, 1 - u - v) < 0.03:
                continue
            point = add(CAMERA, scale(direction, distance))
            normal = normalized(tuple(sum(normal[axis] * weight
                for normal, weight in zip(triangle.shading_normals, (1 - u - v, u, v))) for axis in range(3)))
            normal = normalized(tuple(map(half, normal)))
            cosine = -dot(direction, normal)
            reflected = add(direction, scale(normal, 2 * cosine))
            if cosine < 0.3 or dot(reflected, triangle.normal) < 0.2:
                continue
            # Both signs of a conservative half-millimeter depth-position perturbation must still escape the mesh
            # and ground. The 1mm normal offset matches the fixture's stable captured-glass origin guard.
            origins = tuple(add(add(point, scale(normal, 0.001)), scale(direction, perturbation))
                for perturbation in (-0.0005, 0.0, 0.0005))
            if any(mesh.intersect(origin, reflected) is not None or hits_ground(origin, reflected) for origin in origins):
                continue
            f0 = ((IOR - 1) / (IOR + 1)) ** 2
            selected.append((x, y, f0 + (1 - f0) * (1 - cosine) ** 5))
    return tuple(selected)


def decode_scene_radiance(value):
    encoded = value / 255
    linear = encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4
    return SHOULDER * linear / max(1 - linear, 1e-9)


def expected_environment_color(disabled_rgb, fresnel):
    expected = []
    for value, environment in zip(disabled_rgb, ENVIRONMENT):
        radiance = decode_scene_radiance(value) + environment * fresnel
        linear = radiance / (radiance + SHOULDER)
        encoded = linear * 12.92 if linear <= 0.0031308 else 1.055 * linear ** (1 / 2.4) - 0.055
        expected.append(encoded * 255)
    return tuple(expected)


def predictor_metadata():
    # Canonical LF content identity is stable across the repository's required CRLF checkout policy.
    digest = hashlib.sha256(MESH_PATH.read_text(encoding="utf-8").encode("utf-8")).hexdigest()
    return {"mesh": str(MESH_PATH.relative_to(Path(__file__).parent)), "mesh_lf_sha256": digest,
        "camera": CAMERA, "sphere_center": (0, 0.85, 0), "sphere_scale": 0.7, "vertical_fov_degrees": 60,
        "ior": IOR, "environment": ENVIRONMENT, "presentation": "sRGB of Reinhard scene radiance",
        "exposure": 1.0, "shoulder": SHOULDER, "minimum_fraction_of_predicted_byte_gain": 0.5,
        "rounding_allowance_bytes": 2.0}
