#!/usr/bin/env python3
"""Radiometric oracle for parallel glass slabs separated by a CSG cavity."""

from csg_shadow_reference import region_pixels
from window_capture_smoke import SmokeFailure


TRANSMISSION = (0.25, 0.5, 0.75)
IOR = 2.0
REGIONS = (("split", -1.3, 0.0), ("control", 1.3, 0.0))


def decode_srgb(value):
    encoded = value / 255.0
    return encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4


def expected_split_ratio():
    interface = 1.0 - ((IOR - 1.0) / (IOR + 1.0)) ** 2
    # Removing one unit of glass adds two air/glass interfaces; photons still travel straight at normal incidence.
    return tuple(interface ** 2 / tint for tint in TRANSMISSION)


def measure(frame):
    width, height, rows = frame
    if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure("CSG caustic frame dimensions are invalid")
    output = {}
    for region in REGIONS:
        pixels = region_pixels(region, width, height, spacing=0.045)
        output[region[0]] = tuple(sum(decode_srgb(rows[y][x][channel]) for x, y in pixels) / len(pixels)
            for channel in range(3))
    return output


def relative_match(observed, expected, tolerance, context):
    for channel, (actual, target) in enumerate(zip(observed, expected)):
        if abs(actual - target) > tolerance * target + 0.002:
            raise SmokeFailure(f"{context} channel {channel}: observed {actual:.6f}, expected {target:.6f}")


def analyze_route(frames):
    if set(frames) != {"reference", "cut", "uncut", "disabled"}:
        raise SmokeFailure("CSG caustic comparison requires reference, cut, uncut and disabled captures")
    if len({frame[:2] for frame in frames.values()}) != 1:
        raise SmokeFailure("CSG caustic captures have different extents")
    measurements = {arm: measure(frame) for arm, frame in frames.items()}
    for arm in ("reference", "cut", "uncut"):
        for region in REGIONS:
            signal = measurements[arm][region[0]]
            if min(signal) < 0.004 or max(signal) > 0.85:
                raise SmokeFailure(f"{arm}/{region[0]} has missing or saturated photon signal: {signal}")
    for region in REGIONS:
        if max(measurements["disabled"][region[0]]) > 0.001:
            raise SmokeFailure("disabled capture contains non-photon lighting")
    for region in REGIONS:
        relative_match(measurements["cut"][region[0]], measurements["reference"][region[0]], 0.15,
            "cut/reference " + region[0])
    for arm in ("reference", "cut"):
        relative_match(measurements[arm]["control"], measurements["uncut"]["control"], 0.15,
            arm + "/unchanged control")
        observed = tuple(a / b for a, b in zip(measurements[arm]["split"], measurements["uncut"]["split"]))
        relative_match(observed, expected_split_ratio(), 0.15, arm + "/four-interface Beer-Fresnel ratio")
    return {"linear_irradiance": measurements, "expected_split_uncut_ratio": expected_split_ratio()}


def compare_routes(results):
    for arm in ("reference", "cut", "uncut"):
        for region in REGIONS:
            relative_match(results["hardware"]["linear_irradiance"][arm][region[0]],
                results["software"]["linear_irradiance"][arm][region[0]], 0.18,
                "hardware/software " + arm + "/" + region[0])
