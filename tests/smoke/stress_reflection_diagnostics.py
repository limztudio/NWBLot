"""Validate accepted reflection diagnostic samples from a stress run."""

import re

from window_capture_smoke import SmokeFailure


LIT_BOUNDED_UNSIGNED_FIELD_PATTERN = r"=([0-9]{1,20})"
LIT_UNSUPPORTED_RATIO = "unsupported_ratio"
LIT_EXTERIOR_ELIGIBLE_RATIO = "exterior_eligible_ratio"
LIT_PHYSICAL_QUERIES_PER_HARDWARE_RAY = "physical_queries_per_hardware_ray"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_FRAME = "frame"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_HARDWARE_READY = "hardware_ready"
LIT_TRANSPORT_ENABLED = "transport_enabled"
LIT_CANDIDATES = "candidates"
LIT_HARDWARE_RAYS = "hardware_rays"
LIT_EXTERIOR_ELIGIBLE_RAYS = "exterior_eligible_rays"
LIT_QUERY_BUDGET_UNITS = "query_budget_units"
LIT_PHYSICAL_QUERIES = "physical_queries"
LIT_BOOTSTRAP_EVENTS = "bootstrap_events"
LIT_TRANSPARENT_PATHS = "transparent_paths"
LIT_UNSUPPORTED_PATHS = "unsupported_paths"
LIT_SCREEN_ATTEMPTS = "screen_attempts"
LIT_SCREEN_HITS = "screen_hits"
LIT_SCREEN_RETURNS = "screen_returns"
LIT_SCREEN_ITERATIONS = "screen_iterations"
LIT_SCREEN_LIMIT_MISSES = "screen_limit_misses"
LIT_REQUESTED = "requested"
LIT_STATUS = "status"
LIT_SAMPLE_COUNT = "sample_count"
LIT_SUMS = "sums"
LIT_SCREEN = "screen"
LIT_MAXIMUM_FRAME_AVERAGE_ITERATIONS = "maximum_frame_average_iterations"
REFLECTION_ENABLED = "StressTestSmokeProject: reflection diagnostics enabled"
REFLECTION_SAMPLE = "StressReflectionStatistics: "
REFLECTION_FIELDS = (LIT_SEQUENCE, LIT_GENERATION, LIT_FRAME, LIT_GRAPHICS_FRAME, LIT_HARDWARE_READY, LIT_TRANSPORT_ENABLED, LIT_CANDIDATES, LIT_HARDWARE_RAYS, LIT_EXTERIOR_ELIGIBLE_RAYS, LIT_QUERY_BUDGET_UNITS, LIT_PHYSICAL_QUERIES, LIT_BOOTSTRAP_EVENTS,
    LIT_TRANSPARENT_PATHS, LIT_UNSUPPORTED_PATHS)
REFLECTION_COUNTERS = REFLECTION_FIELDS[6:]
REFLECTION_SCREEN_FIELDS = (LIT_SCREEN_ATTEMPTS, LIT_SCREEN_HITS, LIT_SCREEN_RETURNS, LIT_SCREEN_ITERATIONS, LIT_SCREEN_LIMIT_MISSES)


REFLECTION_QUALITY_SETTINGS = "ReflectionQualitySmoke: requested screen_max_steps="
LIT_VERIFIED = "verified"


class ReflectionDiagnostics:
    def __init__(self, lines, requested, shutdown_marker):
        self.lines = lines
        self.requested = requested
        self.shutdown_marker = shutdown_marker

    def parse(self):
        lines = self.lines
        requested = self.requested
        records = [(index, line) for index, line in enumerate(lines) if line.startswith(REFLECTION_SAMPLE)]
        if not requested:
            if REFLECTION_ENABLED in lines or records:
                raise SmokeFailure("unrequested reflection diagnostics invalidate the performance-only run")
            return {LIT_REQUESTED: False, LIT_STATUS: "not_measured"}
        if lines.count(REFLECTION_ENABLED) != 1 or not records:
            raise SmokeFailure("requested reflection diagnostics require one enable marker and accepted samples")
        enabled_index = lines.index(REFLECTION_ENABLED)
        shutdown_index = lines.index(self.shutdown_marker)
        pattern = re.escape(REFLECTION_SAMPLE) + " ".join(re.escape(field) + LIT_BOUNDED_UNSIGNED_FIELD_PATTERN for field in REFLECTION_FIELDS)
        pattern += " " + " ".join(re.escape(field) + LIT_BOUNDED_UNSIGNED_FIELD_PATTERN for field in REFLECTION_SCREEN_FIELDS)
        sums = dict.fromkeys(REFLECTION_COUNTERS, 0)
        screen_sums = dict.fromkeys(REFLECTION_SCREEN_FIELDS, 0)
        maximum_frame_average_iterations = 0.0
        seen = set()
        ranges = {}
        previous = None
        hardware_ready_samples = transport_enabled_samples = 0
        for index, line in records:
            if not enabled_index < index < shutdown_index:
                raise SmokeFailure("reflection samples must follow enablement and precede shutdown")
            match = re.fullmatch(pattern, line)
            if not match:
                raise SmokeFailure("malformed accepted reflection diagnostic sample")
            row = dict(zip(REFLECTION_FIELDS, map(int, match.groups()[:len(REFLECTION_FIELDS)])))
            screen = dict(zip(REFLECTION_SCREEN_FIELDS, map(int, match.groups()[len(REFLECTION_FIELDS):])))
            for field, value in screen.items():
                if value >= 2 ** (64 if field == LIT_SCREEN_ITERATIONS else 32):
                    raise SmokeFailure("screen reflection counter exceeds its unsigned field bounds")
            attempts = screen[LIT_SCREEN_ATTEMPTS]
            if (screen[LIT_SCREEN_HITS] > screen[LIT_SCREEN_RETURNS] or screen[LIT_SCREEN_RETURNS] > attempts or screen[LIT_SCREEN_LIMIT_MISSES] > attempts or screen[LIT_SCREEN_ITERATIONS] > attempts * 256):
                raise SmokeFailure("screen reflection counters exceed their attempted ray population or maximum budget")
            for field in REFLECTION_SCREEN_FIELDS:
                screen_sums[field] += screen[field]
            maximum_frame_average_iterations = max(maximum_frame_average_iterations, screen[LIT_SCREEN_ITERATIONS] / attempts if attempts else 0.0)
            for field, value in row.items():
                bits = 64 if field in (LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME) else 32
                if value >= 2 ** bits or (field in (LIT_SEQUENCE, LIT_GENERATION) and value == 0):
                    raise SmokeFailure("reflection diagnostic value exceeds its unsigned field bounds")
            if row[LIT_HARDWARE_READY] not in (0, 1) or row[LIT_TRANSPORT_ENABLED] not in (0, 1):
                raise SmokeFailure("reflection diagnostic flags must be zero or one")
            if row[LIT_TRANSPORT_ENABLED] and not row[LIT_HARDWARE_READY]:
                raise SmokeFailure("optical transport cannot be enabled without accepted hardware readiness")
            rays = row[LIT_HARDWARE_RAYS]
            if rays > row[LIT_CANDIDATES] or any(row[field] > rays for field in (LIT_EXTERIOR_ELIGIBLE_RAYS, LIT_TRANSPARENT_PATHS, LIT_UNSUPPORTED_PATHS)):
                raise SmokeFailure("reflection path counters cannot exceed their admitted ray population")
            if not row[LIT_HARDWARE_READY] and any(row[field] for field in REFLECTION_COUNTERS if field != LIT_CANDIDATES):
                raise SmokeFailure("reflection work counters require accepted hardware readiness")
            if not row[LIT_TRANSPORT_ENABLED] and any(row[field] for field in (LIT_EXTERIOR_ELIGIBLE_RAYS, LIT_BOOTSTRAP_EVENTS, LIT_TRANSPARENT_PATHS, LIT_UNSUPPORTED_PATHS)):
                raise SmokeFailure("optical-only counters require optical transport")
            if row[LIT_PHYSICAL_QUERIES] > row[LIT_QUERY_BUDGET_UNITS]:
                raise SmokeFailure("physical reflection queries exceed reserved traversal units")
            if (rays == 0 and row[LIT_QUERY_BUDGET_UNITS] != 0) or (row[LIT_QUERY_BUDGET_UNITS] == 0 and (row[LIT_BOOTSTRAP_EVENTS] != 0 or row[LIT_TRANSPARENT_PATHS] != 0)):
                raise SmokeFailure("reflection query-dependent counters require admitted rays and reserved traversal units")
            key = row[LIT_GENERATION], row[LIT_SEQUENCE]
            if key in seen:
                raise SmokeFailure("duplicate accepted reflection diagnostic sample")
            seen.add(key)
            if previous is not None and row[LIT_GENERATION] == previous[LIT_GENERATION]:
                if row[LIT_SEQUENCE] <= previous[LIT_SEQUENCE] or row[LIT_GRAPHICS_FRAME] <= previous[LIT_GRAPHICS_FRAME]:
                    raise SmokeFailure("accepted reflection sequence and graphics frame must advance within a generation")
            elif row[LIT_GENERATION] in ranges:
                raise SmokeFailure("accepted reflection diagnostics cannot return to an earlier generation")
            generation = ranges.setdefault(row[LIT_GENERATION], {LIT_GENERATION: row[LIT_GENERATION], LIT_SAMPLE_COUNT: 0, "sequence_range": [row[LIT_SEQUENCE], row[LIT_SEQUENCE]], "frame_range": [row[LIT_FRAME], row[LIT_FRAME]],
                "graphics_frame_range": [row[LIT_GRAPHICS_FRAME], row[LIT_GRAPHICS_FRAME]]})
            generation[LIT_SAMPLE_COUNT] += 1
            for field in (LIT_SEQUENCE, LIT_FRAME, LIT_GRAPHICS_FRAME):
                bounds = generation[field + "_range"]
                bounds[0], bounds[1] = min(bounds[0], row[field]), max(bounds[1], row[field])
            for field in REFLECTION_COUNTERS:
                sums[field] += row[field]
            hardware_ready_samples += row[LIT_HARDWARE_READY]
            transport_enabled_samples += row[LIT_TRANSPORT_ENABLED]
            previous = row
        rays = sums[LIT_HARDWARE_RAYS]
        if rays > 0 and sums[LIT_UNSUPPORTED_PATHS] == rays and sums[LIT_PHYSICAL_QUERIES] == 0:
            status = "all_rejected"
        elif sums[LIT_UNSUPPORTED_PATHS] > 0:
            status = "unsupported"
        elif sums[LIT_PHYSICAL_QUERIES] > 0:
            status = "queries_observed"
        else:
            status = "no_queries"
        return {LIT_REQUESTED: True, LIT_STATUS: status, LIT_SAMPLE_COUNT: len(records), "sample_scope": "accepted_readbacks_including_warmup_not_presentation_counts", "ranges_by_generation": list(ranges.values()), "hardware_ready_samples": hardware_ready_samples,
            "transport_enabled_samples": transport_enabled_samples, LIT_SUMS: sums, LIT_UNSUPPORTED_RATIO: sums[LIT_UNSUPPORTED_PATHS] / rays if rays else None, LIT_EXTERIOR_ELIGIBLE_RATIO: sums[LIT_EXTERIOR_ELIGIBLE_RAYS] / rays if rays else None,
            "query_budget_units_per_hardware_ray": sums[LIT_QUERY_BUDGET_UNITS] / rays if rays else None, LIT_PHYSICAL_QUERIES_PER_HARDWARE_RAY: sums[LIT_PHYSICAL_QUERIES] / rays if rays else None, LIT_SCREEN: {LIT_SAMPLE_COUNT: len(records), LIT_SUMS: screen_sums,
                LIT_MAXIMUM_FRAME_AVERAGE_ITERATIONS: maximum_frame_average_iterations, "iterations_per_attempt": screen_sums[LIT_SCREEN_ITERATIONS] / screen_sums[LIT_SCREEN_ATTEMPTS] if screen_sums[LIT_SCREEN_ATTEMPTS] else None,
                "limit_miss_ratio": screen_sums[LIT_SCREEN_LIMIT_MISSES] / screen_sums[LIT_SCREEN_ATTEMPTS] if screen_sums[LIT_SCREEN_ATTEMPTS] else None,
                "return_ratio": screen_sums[LIT_SCREEN_RETURNS] / screen_sums[LIT_SCREEN_ATTEMPTS] if screen_sums[LIT_SCREEN_ATTEMPTS] else None}}

    @staticmethod
    def verify_reflection_quality_settings(text, args, optical):
        records = [line.strip() for line in text.splitlines() if line.strip().startswith(REFLECTION_QUALITY_SETTINGS)]
        if records != [REFLECTION_QUALITY_SETTINGS + str(args.reflection_screen_steps)]:
            raise SmokeFailure("reflection screen step budget mismatch between request and application")
        if args.reflection_diagnostics:
            screen = optical[LIT_SCREEN]
            if screen[LIT_SAMPLE_COUNT] != optical[LIT_SAMPLE_COUNT]:
                raise SmokeFailure("new reflection diagnostic acquisition requires accepted screen-work counters for every sample")
            if screen[LIT_MAXIMUM_FRAME_AVERAGE_ITERATIONS] > args.reflection_screen_steps:
                raise SmokeFailure("accepted screen iteration count exceeds the requested step budget")
        return {"screen_max_steps": args.reflection_screen_steps, LIT_VERIFIED: True, "screen_work_measured": args.reflection_diagnostics}
