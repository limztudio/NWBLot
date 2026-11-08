#!/usr/bin/env python3
"""Float64 geometric and radiometric oracle, independent of the renderer's optical walker."""

import math

from reflection_optical_geometry import (LIT_OPTICAL_CSG_UNSPECIFIED_CAP, LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_CAVITY, LIT_OPTICAL_CSG_OPEN_TAIL,
    LIT_OPTICAL_TIR, LIT_PRIORITY, LIT_UNSPECIFIED, ReflectionOpticalScene, add, dot, half, normalized, scale)

LIT_QUERIES = "queries"
LIT_REASON = "reason"
LIT_ENVIRONMENT = "environment"
LIT_CROSSINGS = "crossings"
LIT_TIR_EVENTS = "tir_events"

LIT_OPTICAL_ALPHA_BEFORE = "optical_alpha_before"
LIT_QUERY_LIMIT = "query_limit"
LIT_BOUNDARY = "boundary"
LIT_CHART = "chart"
LIT_ALPHA = "alpha"
LIT_AMBIGUOUS = "ambiguous"


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


MIRROR_F0 = half(0.95)
CHART_RADIANCE = half(0.9)
DENSE_CASES = ("optical_dense", "optical_csg_dense")
DENSE_RADIANCE = tuple(map(half, (.9, .2, .05)))


def effective_medium(active):
    if not active:
        return None
    if len({item.mode for item in active}) != 1:
        raise ValueError("mixed optical medium contracts")
    return max(active, key=lambda item: (item.priority, -item.tie_order)) if active[0].mode == LIT_PRIORITY else active[-1]


def chart_color(x):
    stripe = math.floor((x + 12.4) / 0.8)
    if stripe < 0 or stripe >= 31:
        return (0.0, 0.0, 0.0)
    return (CHART_RADIANCE,) * 3 if stripe == 15 else tuple(CHART_RADIANCE if channel == stripe % 3 else 0.0 for channel in range(3))


def dense_receiver_intersections(origin, direction):
    # Independent world-space slabs: source boxes are translated by -8.5 and each separated component is one unit thick.
    crossings = []
    for nearest_z in (-8., -10., -12., -14., -16., -18., -20., -22., -24.):
        near, far = -math.inf, math.inf
        for position, component, bounds in zip(origin, direction, ((-12., 12.), (-7.6, 10.4), (nearest_z - 1., nearest_z))):
            if component == 0.:
                if not bounds[0] < position < bounds[1]:
                    near, far = math.inf, -math.inf
                    break
            else:
                first, second = sorted((bound - position) / component for bound in bounds)
                near, far = max(near, first), min(far, second)
        if near < far and far > 0.:
            crossings.extend(parameter for parameter in (near, far) if parameter > 0.)
    return tuple(sorted(crossings))


def csg_receiver_intersects(case, origin, direction, maximum):
    # Even a no-hit collection on the open quad reserves one traversal unit.
    minimum_z = -16. if case == LIT_OPTICAL_CSG_OPEN_TAIL else -10.
    maximum_z = 4. if case == LIT_OPTICAL_CSG_CAVITY else -8.
    near, far = 0., maximum
    for position, component, bounds in zip(origin, direction, ((-12., 12.), (-7.6, 10.4), (minimum_z, maximum_z))):
        if component == 0.:
            if not bounds[0] <= position <= bounds[1]:
                return False
        else:
            first, second = sorted((bound - position) / component for bound in bounds)
            near, far = max(near, first), min(far, second)
    return near < far


def trace_transmission(case, origin, direction, max_queries=16):
    if case in DENSE_CASES:
        intersections = dense_receiver_intersections(origin, direction)
        if not intersections:
            return (0., 0., 0.), {LIT_QUERIES: 1, LIT_REASON: LIT_ENVIRONMENT, LIT_CROSSINGS: 0, LIT_TIR_EVENTS: 0}
        return DENSE_RADIANCE, {LIT_QUERIES: 2 if case == "optical_csg_dense" else 1, LIT_REASON: "opaque_dense", LIT_CROSSINGS: len(intersections), LIT_TIR_EVENTS: 0, "nearest_distance": intersections[0]}

    if case in ("optical_sliver", "optical_csg_sliver", "optical_sub_ulp", "optical_csg_sub_ulp", "optical_group_gap", "optical_csg_group_gap", "optical_group_gap_sub_ulp", "optical_csg_group_gap_sub_ulp", "optical_group_entry", "optical_csg_group_entry"):
        # Unresolved thin spans or gaps retain conservative rejection even when grouped endpoints leave the same nonzero membership.
        return (0.0, 0.0, 0.0), {LIT_QUERIES: 0, LIT_REASON: LIT_AMBIGUOUS, LIT_CROSSINGS: 0, LIT_TIR_EVENTS: 0}
    objects = ReflectionOpticalScene.boundaries(case)
    csg_receiver = case in (LIT_OPTICAL_CSG_CAP, LIT_OPTICAL_CSG_CAVITY, LIT_OPTICAL_CSG_OPEN_TAIL, LIT_OPTICAL_CSG_UNSPECIFIED_CAP)
    active = [item for item in objects if item.contains(origin)]
    # At an interior start, ascending entry distance along the opposite direction reconstructs outer->inner membership.
    active.sort(key=lambda item: item.intersect(origin, scale(direction, -1))[0], reverse=True)
    used_queries = 1 if active else 0
    throughput, radiance = [1.0] * 3, [0.0] * 3
    alpha_pending = case in (LIT_OPTICAL_ALPHA_BEFORE, "optical_alpha_after")
    tir_events, crossings = 0, 0
    reason = LIT_ENVIRONMENT
    for _ in range(32):
        if used_queries >= max_queries:
            reason = LIT_QUERY_LIMIT
            break
        used_queries += 1
        events = []
        for item in objects:
            hit = item.intersect(origin, direction)
            if hit is not None:
                events.append((hit[0], LIT_BOUNDARY, item, hit[1]))
        if direction[2] < -1e-10:
            chart_distance = (-14.0 - origin[2]) / direction[2]
            if chart_distance > 1e-6 and case != LIT_OPTICAL_TIR:
                events.append((chart_distance, LIT_CHART, None, None))
            if alpha_pending:
                pane_distance = ((-7.0 if case == LIT_OPTICAL_ALPHA_BEFORE else -11.0) - origin[2]) / direction[2]
                if pane_distance > 1e-6:
                    events.append((pane_distance, LIT_ALPHA, None, None))
        closest_distance = min((event[0] for event in events), default=1000.)
        if csg_receiver and csg_receiver_intersects(case, origin, direction, closest_distance):
            if used_queries >= max_queries:
                reason = LIT_QUERY_LIMIT
                break
            used_queries += 1
        if not events:
            if active:
                reason = "invalid_exit"
            else:
                radiance = [value + weight * (1.0 if case == LIT_OPTICAL_TIR else 0.0) for value, weight in zip(radiance, throughput)]
            break
        distance, kind, item, outward = min(events, key=lambda event: event[0])
        try:
            current = effective_medium(active)
        except ValueError:
            reason = LIT_AMBIGUOUS
            break
        if current is not None:
            throughput = [weight * transmission ** distance for weight, transmission in zip(throughput, current.transmission)]
        hit_point = add(origin, scale(direction, distance))
        if kind == LIT_CHART:
            radiance = [value + weight * color for value, weight, color in zip(radiance, throughput, chart_color(hit_point[0]))]
            reason = LIT_CHART
            break
        if kind == LIT_BOUNDARY and item.mode == LIT_UNSPECIFIED and item.ior != 1.:
            crossings += 1
            reason = "unsupported"
            break
        # A retained CSG interface reserves its ordinary tie query plus two receiver checks even when closed crossings are reused.
        boundary_checks = 3 if csg_receiver and kind == LIT_BOUNDARY else 1
        for _ in range(boundary_checks):
            if used_queries >= max_queries:
                reason = LIT_QUERY_LIMIT
                break
            used_queries += 1
        if reason == LIT_QUERY_LIMIT:
            break
        if sum(event[1] == LIT_BOUNDARY and abs(event[0] - distance) < 1e-9 for event in events) > 1:
            reason = LIT_AMBIGUOUS
            break
        if kind == LIT_ALPHA:
            coverage = 0.0 if hit_point[0] < -2.0 else 0.5 if hit_point[0] < 2.0 else 1.0
            pane_color = tuple(map(half, (0.9, 0.6, 0.0)))
            radiance = [value + weight * coverage * color for value, weight, color in zip(radiance, throughput, pane_color)]
            throughput = [weight * (1.0 - coverage) for weight in throughput]
            alpha_pending = False
            if coverage == 1.0:
                reason = "opaque_alpha"
                break
        elif item.mode == LIT_UNSPECIFIED:
            # Zero-coverage air interfaces still require tie checks at both crossings.
            crossings += 1
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
                reason = LIT_AMBIGUOUS
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
    return tuple(radiance), {LIT_QUERIES: used_queries, LIT_CROSSINGS: crossings, LIT_TIR_EVENTS: tir_events, LIT_REASON: reason}


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
