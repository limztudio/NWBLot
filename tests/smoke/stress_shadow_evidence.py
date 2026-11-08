"""Validation of software shadow and receiver quality evidence."""

import re

from window_capture_outcome import SmokeFailure


LIT_CAPTURE_CADENCE = "capture_cadence"
LIT_REFERENCE_THREE = "reference_three"
LIT_TEMPORAL_ONE = "temporal_one"
LIT_QUARTER = "quarter"
LIT_AUTOMATIC = "automatic"
LIT_TRACE = "trace"
LIT_REFERENCE = "reference"
LIT_REFERENCE_GRID9 = "reference_grid9"
LIT_EVERY_FRAME = "every_frame"
LIT_REQUESTED = "requested"
LIT_OBSERVED = "observed"
LIT_VERIFIED = "verified"
SHADOW_QUALITY_SETTINGS = "ShadowQualitySmoke: requested transparent_sampling="
SHADOW_TEMPORAL_ONE_RECORDED = "RendererSystem: recorded temporal-one transparent shadow sampling "
SHADOW_TRANSPARENT_SAMPLING = {LIT_REFERENCE_THREE: 0, LIT_TEMPORAL_ONE: 1}
SHADOW_RECEIVER_RESOLUTION = {"half": 2, LIT_QUARTER: 4}
SHADOW_RECEIVER_GRID = "RendererSystem: created shadow receiver grid "
SOFTWARE_SHADOW_SETTINGS = "SoftwareShadowSmoke: requested "
SOFTWARE_SHADOW_BACKENDS = {LIT_AUTOMATIC: 0, LIT_TRACE: 1, "light_space": 2}
SOFTWARE_SHADOW_COVERAGE = {LIT_REFERENCE: 0, "fitted_volume": 1}
SOFTWARE_SHADOW_BLOCKER_SEARCH = {LIT_REFERENCE_GRID9: 0, "compact_cross5": 1, "center1": 2}
SOFTWARE_SHADOW_CAPTURE_CADENCE = {LIT_EVERY_FRAME: 0, "reuse_one_frame": 1, "reuse_two_frames": 2}
SOFTWARE_SHADOW_CAPTURE_REUSE_PREFIX = "RendererSystem: accepted light-space capture reuse "


class ShadowEvidence:
    def __init__(self, args):
        self._args = args

    def verify_software(self, text):
        """New acquisitions must prove the requested settings reached the application."""
        args = self._args
        records = [line.strip() for line in text.splitlines() if line.strip().startswith(SOFTWARE_SHADOW_SETTINGS)]
        if len(records) != 1:
            raise SmokeFailure("exactly one SoftwareShadowSmoke requested-settings record is required")
        fields = ("backend", "directional_resolution", "point_resolution", "budget_bytes", "coverage", "blocker_search", LIT_CAPTURE_CADENCE)
        pattern = re.escape(SOFTWARE_SHADOW_SETTINGS) + " ".join(re.escape(field) + r"=([0-9]+)" for field in fields)
        match = re.fullmatch(pattern, records[0])
        if not match:
            raise SmokeFailure("malformed SoftwareShadowSmoke requested-settings record")
        observed = dict(zip(fields, map(int, match.groups())))
        requested = dict(backend=SOFTWARE_SHADOW_BACKENDS[args.software_shadow_backend], directional_resolution=args.software_shadow_directional_resolution,
            point_resolution=args.software_shadow_point_resolution, budget_bytes=args.software_shadow_budget_mib * 1024 * 1024, coverage=SOFTWARE_SHADOW_COVERAGE[args.software_shadow_coverage],
            blocker_search=SOFTWARE_SHADOW_BLOCKER_SEARCH[args.software_shadow_blocker_search], capture_cadence=SOFTWARE_SHADOW_CAPTURE_CADENCE[args.software_shadow_capture_cadence])
        if observed != requested:
            raise SmokeFailure(f"software shadow settings mismatch: requested {requested}, application reported {observed}")
        reuse_records = [line.strip() for line in text.splitlines() if line.strip().startswith(SOFTWARE_SHADOW_CAPTURE_REUSE_PREFIX)]
        expected_reuse = SOFTWARE_SHADOW_CAPTURE_REUSE_PREFIX + f"(cadence={requested[LIT_CAPTURE_CADENCE] + 1})"
        if args.software_shadow_capture_cadence != LIT_EVERY_FRAME and reuse_records != [expected_reuse]:
            raise SmokeFailure("capture cadence requires exactly one matching accepted light-space capture reuse marker")
        if args.software_shadow_capture_cadence == LIT_EVERY_FRAME and reuse_records:
            raise SmokeFailure("every-frame acquisition unexpectedly reused a light-space capture")
        return {"capture_cadence_name": args.software_shadow_capture_cadence, "accepted_reuse_verified": bool(reuse_records), "backend_name": args.software_shadow_backend, "budget_mib": args.software_shadow_budget_mib,
            "blocker_search_name": args.software_shadow_blocker_search, LIT_REQUESTED: requested, LIT_OBSERVED: observed, LIT_VERIFIED: True}


    def verify_quality(self, text, expected_extent=None):
        args = self._args
        records = [line.strip() for line in text.splitlines() if line.strip().startswith(SHADOW_QUALITY_SETTINGS)]
        if len(records) != 1:
            raise SmokeFailure("exactly one ShadowQualitySmoke requested-settings record is required")
        factor = SHADOW_RECEIVER_RESOLUTION[args.shadow_receiver_resolution]
        expected = SHADOW_QUALITY_SETTINGS + str(SHADOW_TRANSPARENT_SAMPLING[args.shadow_transparent_sampling]) + f" receiver_factor={factor}"
        if records[0] != expected:
            raise SmokeFailure("shadow sampling quality mismatch between request and application")
        lines = [line.strip() for line in text.splitlines()]
        grids = [line for line in lines[lines.index(expected) + 1:] if line.startswith(SHADOW_RECEIVER_GRID)]
        if not grids:
            raise SmokeFailure("shadow quality requires an allocated receiver-grid record after the request")
        allocations = []
        for grid in grids:
            match = re.fullmatch(re.escape(SHADOW_RECEIVER_GRID) + r"factor=([24]) full=(\d+)x(\d+) receiver=(\d+)x(\d+)", grid)
            if not match:
                raise SmokeFailure("malformed shadow receiver-grid allocation")
            actual, width, height, reduced_width, reduced_height = map(int, match.groups())
            if actual != factor or min(width, height, reduced_width, reduced_height) < 1:
                raise SmokeFailure("shadow receiver factor or dimensions disagree with requested quality")
            if expected_extent is not None and (width, height) != tuple(expected_extent):
                raise SmokeFailure("shadow receiver source disagrees with measured frame extent")
            if (reduced_width, reduced_height) != ((width + factor - 1) // factor, (height + factor - 1) // factor):
                raise SmokeFailure("shadow receiver grid does not cover the source at the requested factor")
            allocations.append(dict(factor=factor, width=width, height=height, reduced_width=reduced_width, reduced_height=reduced_height))
        dispatched = [line.strip() for line in text.splitlines() if line.strip().startswith(SHADOW_TEMPORAL_ONE_RECORDED)]
        hardware = None
        if args.shadow_transparent_sampling == LIT_TEMPORAL_ONE:
            if len(dispatched) != 1:
                raise SmokeFailure("temporal-one acquisition requires exactly one effective dispatch marker")
            match = re.fullmatch(re.escape(SHADOW_TEMPORAL_ONE_RECORDED) + r"samples=1 hardware=([01])", dispatched[0])
            if not match:
                raise SmokeFailure("invalid effective temporal-one dispatch marker")
            hardware = bool(int(match[1]))
        elif dispatched:
            raise SmokeFailure("unrequested temporal-one dispatch was recorded")
        return {"transparent_sampling": args.shadow_transparent_sampling, "receiver_resolution": args.shadow_receiver_resolution, "receiver_factor": factor, "allocations": allocations,
            "bootstrap_samples": 3, "accepted_history_samples": 1 if args.shadow_transparent_sampling == LIT_TEMPORAL_ONE else 3, "effective_dispatch_verified": bool(dispatched), "hardware": hardware, LIT_VERIFIED: True}

