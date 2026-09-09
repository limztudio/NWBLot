#!/usr/bin/env python3
"""Float64 geometric and radiometric oracle, independent of the renderer's optical walker."""

from dataclasses import dataclass
from functools import lru_cache
import json
import math
from pathlib import Path
import re
import struct


def half(value):
    return struct.unpack("e", struct.pack("e", value))[0]


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


def fresnel_snell(direction, incident_normal, eta_i, eta_t):
    """Exact unpolarized dielectric Fresnel; None direction denotes total internal reflection."""
    cosine_i = max(0.0, min(1.0, -dot(direction, incident_normal)))
    eta = eta_i / eta_t
    sine_t_squared = eta * eta * (1.0 - cosine_i * cosine_i)
    if sine_t_squared >= 1.0:
        return 1.0, None
    cosine_t = math.sqrt(1.0 - sine_t_squared)
    rs = (eta_i * cosine_i - eta_t * cosine_t) / (eta_i * cosine_i + eta_t * cosine_t)
    rp = (eta_t * cosine_i - eta_i * cosine_t) / (eta_t * cosine_i + eta_i * cosine_t)
    direction_t = add(scale(direction, eta), scale(incident_normal, eta * cosine_i - cosine_t))
    return (rs * rs + rp * rp) * 0.5, normalized(direction_t)


@dataclass(frozen=True)
class Boundary:
    name: str
    ior: float
    transmission: tuple
    planes: tuple = ()
    triangles: tuple = ()
    mode: str = "nested"
    priority: int = 0
    tie_order: int = 0

    def contains(self, point):
        if self.planes:
            return all(dot(normal, point) < distance - 1e-7 for normal, distance in self.planes)
        # The authored torus is behind the camera; none of these reference rays starts inside it.
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
MIRROR_F0 = half(0.95)
CHART_RADIANCE = half(0.9)


def box(name, center_z, thickness, ior=1.5, transmission=TINTED, mode="nested", priority=0, yaw=0.0, tie_order=0, xy_scale=1.0):
    cosine, sine = math.cos(yaw), math.sin(yaw)
    axes = ((cosine, 0.0, -sine), (0.0, 1.0, 0.0), (sine, 0.0, cosine))
    center = (0.0, 1.4, center_z)
    planes = tuple((scale(axis, sign), extent + dot(scale(axis, sign), center))
        for axis, extent in zip(axes, (12.0 * xy_scale, 9.0 * xy_scale, thickness * 0.5)) for sign in (-1.0, 1.0))
    return Boundary(name, half(ior), tuple(map(half, transmission)), planes, mode=mode, priority=priority, tie_order=tie_order)


@lru_cache(maxsize=1)
def authored_torus_triangles():
    source = (Path(__file__).parent / "assets/meshes/refraction_torus.nwb").read_text(encoding="utf-8")
    def rows(field):
        text = re.search(r"asset\." + field + r"\s*=\s*(\[.*?\]);", source, re.S).group(1)
        return json.loads(re.sub(r",\s*]", "]", text))
    # The instance scale is 2 and its X rotation is -pi/2. This preserves the authored geometric triangle normals.
    positions = [(2 * x, 1.4 + 2 * z, -9.5 - 2 * y) for x, y, z in rows("positions")]
    references = rows("vertex_refs")
    triangles = []
    for indices in rows("indices"):
        a, b, c = (positions[references[index][0]] for index in indices)
        edge_b, edge_c = add(b, scale(a, -1)), add(c, scale(a, -1))
        triangles.append((a, edge_b, edge_c, normalized(cross(edge_b, edge_c))))
    return tuple(triangles)


@lru_cache(maxsize=32)
def boundaries(case):
    if case == "optical_reference":
        return ()
    if case == "optical_tir":
        inverse_root_two = math.sqrt(0.5)
        planes = (((1.0, 0.0, 0.0), 20.0), ((0.0, 0.0, 1.0), 2.0),
            ((-inverse_root_two, 0.0, -inverse_root_two), 8 * inverse_root_two),
            ((0.0, 1.0, 0.0), 11.4), ((0.0, -1.0, 0.0), 8.6))
        return (Boundary("prism", half(1.5), CLEAR, planes),)
    if case.startswith("optical_inside"):
        outer = box("outer", -3, 14, 1.5, CLEAR)
        return (outer, box("inner", -3, 10, 1.33, CLEAR, xy_scale=0.7)) if case.endswith("nested") else (outer,)
    if case in ("optical_same_mesh", "optical_disconnected"):
        # ONE instance still contains two disconnected physical components; re-entry is a fresh interval.
        return (box("front", -8, 0.5), box("back", -10, 0.5))
    if case == "optical_union_single":
        return (box("single", -9, 0.8),)
    if case == "optical_union_same_mesh":
        return (box("front_component", -8.85, 0.5), box("back_component", -9.15, 0.5))
    if case == "optical_coincident_independent":
        return (box("independent_first", -9, 2), box("independent_second", -9, 2))
    if case == "optical_torus":
        return (Boundary("torus", half(1.5), TINTED, triangles=authored_torus_triangles()),)
    if case in ("optical_priority_tie_a", "optical_priority_tie_b"):
        reverse = case == "optical_priority_tie_b"
        return (box("first", -8.5, 2, mode="priority", priority=10, tie_order=int(reverse)),
            box("second", -9.5, 2, 1.33, WARM, "priority", 10, tie_order=int(not reverse)))
    if case in ("optical_priority_a", "optical_priority_b", "optical_mixed"):
        first_wins = case == "optical_priority_a"
        return (box("first", -8.5, 2, mode="priority", priority=20 if first_wins else 10),
            box("second", -9.5, 2, 1.33, WARM, "nested" if case == "optical_mixed" else "priority", 10 if first_wins else 20))
    if case in ("optical_nested2", "optical_nested3", "optical_overflow"):
        count = 5 if case == "optical_overflow" else 3 if case == "optical_nested3" else 2
        return tuple(box(str(index), -9, 4 - 0.6 * index if count == 5 else 3 / (1 << index),
            1.5 if index == 0 else 1.33 if index == 1 else 1.1 + 0.02 * (index - 2), TINTED if index == 0 else WARM,
            xy_scale=1.0 - 0.1 * index)
            for index in range(count))
    return (box("single", -9, 2, transmission=CLEAR if case == "optical_clear" else TINTED,
        mode="unspecified" if case == "optical_unspecified" else "nested", yaw=0.2 if case == "optical_tilted" else 0.0),)


def effective_medium(active):
    if not active:
        return None
    if len({item.mode for item in active}) != 1:
        raise ValueError("mixed optical medium contracts")
    return max(active, key=lambda item: (item.priority, -item.tie_order)) if active[0].mode == "priority" else active[-1]


def chart_color(x):
    stripe = math.floor((x + 12.4) / 0.8)
    if stripe < 0 or stripe >= 31:
        return (0.0, 0.0, 0.0)
    return (CHART_RADIANCE,) * 3 if stripe == 15 else tuple(CHART_RADIANCE if channel == stripe % 3 else 0.0 for channel in range(3))


def trace_transmission(case, origin, direction, max_queries=16):
    objects = boundaries(case)
    active = [item for item in objects if item.contains(origin)]
    # At an interior start, ascending entry distance along the opposite direction reconstructs outer->inner membership.
    active.sort(key=lambda item: item.intersect(origin, scale(direction, -1))[0], reverse=True)
    used_queries = 1 if active else 0
    throughput, radiance = [1.0] * 3, [0.0] * 3
    alpha_pending = case in ("optical_alpha_before", "optical_alpha_after")
    tir_events, crossings = 0, 0
    reason = "environment"
    for _ in range(32):
        if used_queries >= max_queries:
            reason = "query_limit"
            break
        used_queries += 1
        events = []
        for item in objects:
            hit = item.intersect(origin, direction)
            if hit is not None:
                events.append((hit[0], "boundary", item, hit[1]))
        if direction[2] < -1e-10:
            chart_distance = (-14.0 - origin[2]) / direction[2]
            if chart_distance > 1e-6 and case != "optical_tir":
                events.append((chart_distance, "chart", None, None))
            if alpha_pending:
                pane_distance = ((-7.0 if case == "optical_alpha_before" else -11.0) - origin[2]) / direction[2]
                if pane_distance > 1e-6:
                    events.append((pane_distance, "alpha", None, None))
        if not events:
            if active:
                reason = "invalid_exit"
            else:
                radiance = [value + weight * (1.0 if case == "optical_tir" else 0.0) for value, weight in zip(radiance, throughput)]
            break
        distance, kind, item, outward = min(events, key=lambda event: event[0])
        try:
            current = effective_medium(active)
        except ValueError:
            reason = "ambiguous"
            break
        if current is not None:
            throughput = [weight * transmission ** distance for weight, transmission in zip(throughput, current.transmission)]
        hit_point = add(origin, scale(direction, distance))
        if kind == "chart":
            radiance = [value + weight * color for value, weight, color in zip(radiance, throughput, chart_color(hit_point[0]))]
            reason = "chart"
            break
        if kind == "boundary" and item.mode == "unspecified":
            crossings += 1
            reason = "unsupported"
            break
        # A transparent interface also performs a bounded coincidence query before any interface physics.
        if used_queries >= max_queries:
            reason = "query_limit"
            break
        used_queries += 1
        if sum(event[1] == "boundary" and abs(event[0] - distance) < 1e-9 for event in events) > 1:
            reason = "ambiguous"
            break
        if kind == "alpha":
            coverage = 0.0 if hit_point[0] < -2.0 else 0.5 if hit_point[0] < 2.0 else 1.0
            pane_color = tuple(map(half, (0.9, 0.6, 0.0)))
            radiance = [value + weight * coverage * color for value, weight, color in zip(radiance, throughput, pane_color)]
            throughput = [weight * (1.0 - coverage) for weight in throughput]
            alpha_pending = False
            if coverage == 1.0:
                reason = "opaque_alpha"
                break
        else:
            crossings += 1
            entering = dot(direction, outward) < 0.0
            proposed = active + [item] if entering else [other for other in active if other is not item]
            if len(proposed) > 4:
                reason = "medium_overflow"
                break
            try:
                destination = effective_medium(proposed)
            except ValueError:
                reason = "ambiguous"
                break
            eta_i, eta_t = current.ior if current else 1.0, destination.ior if destination else 1.0
            incident_normal = outward if entering else scale(outward, -1.0)
            fresnel, transmitted = fresnel_snell(direction, incident_normal, eta_i, eta_t)
            if transmitted is None:
                tir_events += 1
                direction = add(direction, scale(incident_normal, -2.0 * dot(direction, incident_normal)))
            else:
                throughput = [weight * (1.0 - fresnel) * (eta_i / eta_t) ** 2 for weight in throughput]
                direction, active = transmitted, proposed
        origin = add(hit_point, scale(direction, 2e-6))
    return tuple(radiance), {"queries": used_queries, "crossings": crossings, "tir_events": tir_events, "reason": reason}


def pixel_reference(case, x, y, width=960, height=720, max_queries=16):
    focal = height / (2.0 * math.tan(math.pi / 6.0))
    point = ((x - width * 0.5) * 6.0 / focal, 1.4 - (y - height * 0.5) * 6.0 / focal, 0.0)
    direction = normalized((point[0], point[1] - 1.4, -6.0))
    value, evidence = trace_transmission(case, point, direction, max_queries)
    f0 = MIRROR_F0 + (1.0 - MIRROR_F0) * (1.0 + direction[2]) ** 5
    return tuple(channel * f0 for channel in value), evidence


def encode_radiance(value):
    mapped = max(0.0, value) / (1.0 + max(0.0, value))
    encoded = mapped * 12.92 if mapped <= 0.0031308 else 1.055 * mapped ** (1 / 2.4) - 0.055
    return round(255 * encoded)


def decode_radiance(value):
    encoded = value / 255.0
    mapped = encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4
    return mapped / (1.0 - mapped) if mapped < 1.0 else math.inf
