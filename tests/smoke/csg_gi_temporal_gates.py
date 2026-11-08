"""Apply quality and geometry gates to temporal CSG evidence."""

from csg_gi_temporal_contract import (
    CAP_STATE_REQUIRED_PROBES,
    GEOMETRY_LIMITS,
    GEOMETRY_PROBES,
    LIT_BACKGROUND,
    LIT_BOX,
    LIT_CAPSULE,
    LIT_CAP_STATE,
    LIT_COMPONENT_TOLERANCE,
    LIT_DECODED,
    LIT_DIAGNOSTIC_POINTS,
    LIT_DIRECT,
    LIT_FRAMES,
    LIT_GRAPHICS_FRAME,
    LIT_HORIZONTAL_JUMP_FRACTION,
    LIT_MEAN_RGB,
    LIT_MOTION,
    LIT_NORMAL,
    LIT_OVERLAPPING,
    LIT_P95_TEMPORAL_RANGE,
    LIT_POSITION,
    LIT_REGIONS,
    LIT_SPAN_STATE,
    LIT_SPHERE,
    LIT_STATE,
    LIT_STATIC,
    LIT_VERTICAL_JUMP_FRACTION,
    LIT_VIEW,
    QUALITY_LIMITS,
    SPAN_EXPECTATIONS,
    STATE_CHANNELS,
)
from window_capture_smoke import SmokeFailure


class CsgTemporalGates:
    @staticmethod
    def check_quality(result):
        failures = []
        if result[LIT_MOTION] == LIT_STATIC:
            for name, metrics in result[LIT_REGIONS].items():
                if metrics[LIT_P95_TEMPORAL_RANGE] > QUALITY_LIMITS["static_p95_temporal_range"]:
                    failures.append(f"{name}: settled temporal range exceeds 6 channel levels")
        for frame in result[LIT_FRAMES]:
            prefix = f"frame {frame[LIT_GRAPHICS_FRAME]}"
            background = frame[LIT_REGIONS][LIT_BACKGROUND][LIT_MEAN_RGB]
            if any(abs(value - expected) > QUALITY_LIMITS["background_tolerance"] for value, expected in zip(background, (88, 98, 113))):
                failures.append(f"{prefix}: background differs from the fixed scene")
            for name, metrics in frame[LIT_REGIONS].items():
                if max(metrics[LIT_HORIZONTAL_JUMP_FRACTION], metrics[LIT_VERTICAL_JUMP_FRACTION]) > QUALITY_LIMITS["max_frame_jump_fraction"]:
                    failures.append(f"{prefix} {name}: spatial channel jumps affect more than 10% of neighboring samples")
            for name in (LIT_BOX, LIT_SPHERE, LIT_CAPSULE):
                red, green, blue = frame[LIT_REGIONS][name][LIT_MEAN_RGB]
                if result[LIT_VIEW] == LIT_DIRECT:
                    if max(red, green, blue) < QUALITY_LIMITS["direct_core_minimum"]:
                        failures.append(f"{prefix} {name}: direct control cap is nearly black")
                    if name in (LIT_BOX, LIT_CAPSULE) and max(abs(value - backdrop) for value, backdrop in zip((red, green, blue), background)) < QUALITY_LIMITS["direct_background_separation"]:
                        failures.append(f"{prefix} {name}: direct control cap is indistinguishable from background")
                    continue
                dominant = green if name == LIT_SPHERE else red
                margin = QUALITY_LIMITS["tint_margin"]
                valid_tint = (red >= green + margin and green >= blue + margin) if name == LIT_BOX else (green >= max(red, blue) + margin if name == LIT_SPHERE else red >= max(green, blue) + margin)
                if dominant < QUALITY_LIMITS["colored_core_minimum"] or not valid_tint:
                    failures.append(f"{prefix} {name}: expected colored GI contribution is missing")
        return failures

    @staticmethod
    def check_geometry(result):
        if result[LIT_MOTION] != LIT_STATIC or result[LIT_VIEW] not in (LIT_NORMAL, LIT_POSITION):
            raise SmokeFailure("geometry regression requires a static normal or position view")
        failures = []
        for frame in result[LIT_FRAMES]:
            for name, probe in GEOMETRY_PROBES.items():
                point = frame[LIT_DIAGNOSTIC_POINTS][name]
                expected = probe[result[LIT_VIEW]]
                errors = [abs(value - target) for value, target in zip(point[LIT_DECODED], expected)]
                prefix = f"frame {frame[LIT_GRAPHICS_FRAME]} {name}"
                if result[LIT_VIEW] == LIT_NORMAL:
                    error = sum(value * value for value in errors) ** .5
                    if error > GEOMETRY_LIMITS["normal_l2_error"]:
                        failures.append(f"{prefix}: physical cap normal L2 error {error:.6f} exceeds 0.04")
                elif any(error > tolerance for error, tolerance in zip(errors, point[LIT_COMPONENT_TOLERANCE])):
                    failures.append(f"{prefix}: physical cap position component errors {errors} exceed display-quantization tolerances {point[LIT_COMPONENT_TOLERANCE]}")
        return failures

    @staticmethod
    def check_cap_state(result):
        if result[LIT_MOTION] != LIT_STATIC or result[LIT_VIEW] != LIT_CAP_STATE:
            raise SmokeFailure("cap-state regression requires a static cap_state view")
        failures = []
        for frame in result[LIT_FRAMES]:
            for name, (x, y) in CAP_STATE_REQUIRED_PROBES.items():
                point = frame[LIT_DIAGNOSTIC_POINTS][name]
                for channel in STATE_CHANNELS[LIT_CAP_STATE]:
                    if not point[LIT_STATE][channel]:
                        failures.append(f"frame {frame[LIT_GRAPHICS_FRAME]} {name} ({x},{y}): {channel} is false")
        return failures

    @staticmethod
    def check_spans(result):
        if result[LIT_MOTION] != LIT_STATIC or result[LIT_VIEW] != LIT_SPAN_STATE or not result[LIT_OVERLAPPING]:
            raise SmokeFailure("span regression requires a static overlapping span_state view")
        failures = []
        for frame in result[LIT_FRAMES]:
            for name, expected in SPAN_EXPECTATIONS.items():
                point = frame[LIT_DIAGNOSTIC_POINTS][name]
                for channel, target in expected.items():
                    if point[channel] != target:
                        failures.append(f"frame {frame[LIT_GRAPHICS_FRAME]} {name}: {channel}={point[channel]} expected {target}")
        return failures
