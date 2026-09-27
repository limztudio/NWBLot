#!/usr/bin/env python3
"""Explicit surfel resolve quality and recorded target/dispatch-size verification."""

import re

from window_capture_smoke import SmokeFailure


RESOLUTIONS = {"half": 2, "quarter": 4}
SETTING_MARKER = "SurfelGiQualitySmoke: requested resolve_factor="
DISPATCH_MARKER = "RendererSystem: dispatched surfel GI resolve "
DISPATCH_PATTERN = re.compile(re.escape(DISPATCH_MARKER) +
    r"\(factor=([24]), source=(\d+)x(\d+), resolve=(\d+)x(\d+)\)")


def add_arguments(parser):
    parser.add_argument("--surfel-gi-resolve-resolution", choices=tuple(RESOLUTIONS), default="half",
        help="Quarter resolves one sixteenth of full-resolution pixels; tracing and bilateral full-resolution upsample stay unchanged.")


def verify_settings(text, resolution, expected_extent=None):
    if resolution not in RESOLUTIONS:
        raise SmokeFailure("unsupported surfel GI resolve resolution")
    factor = RESOLUTIONS[resolution]
    records = [line.strip() for line in text.splitlines() if line.strip().startswith(SETTING_MARKER)]
    if records != [SETTING_MARKER + str(factor)]:
        raise SmokeFailure("surfel GI requested quality does not match application settings")
    records = [line.strip() for line in text.splitlines() if line.strip().startswith(DISPATCH_MARKER)]
    if not records:
        raise SmokeFailure("surfel GI quality requires a recorded resolve dispatch")
    dispatches = []
    for record in records:
        match = DISPATCH_PATTERN.fullmatch(record)
        if not match:
            raise SmokeFailure("malformed surfel GI resolve dispatch")
        actual_factor, width, height, reduced_width, reduced_height = map(int, match.groups())
        if actual_factor != factor or min(width, height, reduced_width, reduced_height) < 1:
            raise SmokeFailure("surfel GI resolve factor or dimensions disagree with requested quality")
        if expected_extent is not None and (width, height) != tuple(expected_extent):
            raise SmokeFailure("surfel GI source extent disagrees with the measured frame extent")
        if reduced_width != (width + factor - 1) // factor or reduced_height != (height + factor - 1) // factor:
            raise SmokeFailure("surfel GI reduced dispatch does not cover the source extent at the requested factor")
        dispatches.append(dict(factor=factor, width=width, height=height,
            reduced_width=reduced_width, reduced_height=reduced_height))
    return dict(resolve_resolution=resolution, resolve_factor=factor, verified=True, dispatches=dispatches)
