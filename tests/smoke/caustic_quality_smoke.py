#!/usr/bin/env python3
"""Explicit caustic sampling controls and actual producer-budget verification."""

import re

from window_capture_smoke import SmokeFailure


SETTING_MARKER = "CausticQualitySmoke: requested photon_grid_divisor="
PRODUCER_PATTERN = re.compile(r"RendererSystem: dispatched (hardware|software) caustic producer "
    r"\((\d+) photons/frame, (\d+) temporal phases, (\d+) full-grid budget, "
    r"(\d+) caustic lights, (\d+) refractive instances\)")


def add_arguments(parser):
    parser.add_argument("--caustic-photon-grid-divisor", type=int, choices=(1, 2, 4), default=1,
        help="Divide both photon-grid dimensions; 2 traces one quarter, 4 one sixteenth of the full grid.")


def verify_settings(text, divisor, producer_enabled=True):
    records = [line.partition(SETTING_MARKER)[2].strip() for line in text.splitlines() if SETTING_MARKER in line]
    if records != [str(divisor)]:
        raise SmokeFailure(f"caustic quality settings mismatch: requested {divisor}, application reported {records}")
    matches = list(PRODUCER_PATTERN.finditer(text))
    markers = sum(" caustic producer (" in line for line in text.splitlines())
    if markers != len(matches) or bool(matches) != producer_enabled:
        raise SmokeFailure("missing, malformed, or unexpected actual caustic producer budget")
    producers = []
    for match in matches:
        backend = match[1]
        photons, phases, full_grid, lights, targets = map(int, match.groups()[1:])
        supported_grids = (512,) if backend == "hardware" else (128, 512)
        expected_budgets = {(side // divisor) ** 2 for side in supported_grids}
        if phases not in (1, 2, 4) or full_grid not in expected_budgets or photons * phases != full_grid:
            raise SmokeFailure("actual caustic dispatch budget does not match requested quality or temporal phase")
        if lights <= 0 or targets <= 0:
            raise SmokeFailure("caustic producer reported no emitting lights or refractive targets")
        producers.append(dict(backend=backend, photons_per_frame=photons, temporal_phases=phases,
            full_grid_budget=full_grid, caustic_lights=lights, refractive_instances=targets))
    return dict(photon_grid_divisor=divisor, verified=True, producers=producers)
