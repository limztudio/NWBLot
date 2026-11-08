"""Compare optical framebuffer captures with the independent image oracle."""

import math

from reflection_image_analysis import ReflectionImageAnalysis
from reflection_optical_reference import DENSE_CASES, decode_radiance, pixel_reference
from reflection_optical_evidence import LIT_OPTICAL_TIR
from window_capture_smoke import SmokeFailure


LIT_OPTICAL_TORUS = "optical_torus"
LIT_REASON = "reason"
LIT_CHART = "chart"
LIT_CROSSINGS = "crossings"
DENSE_RECEIVER_CROSSINGS = 18


class ReflectionOpticalImageEvidence:
    @staticmethod
    def reference_points(spec):
        if spec.case in DENSE_CASES:
            # Central rays remain inside every receiver slab through z=-25, beyond the ordinary chart at z=-14.
            return ((x, y) for y in range(280, 441, 8) for x in range(352, 609, 8))
        if spec.case == LIT_OPTICAL_TORUS:
            return ((x, y) for y in range(332, 389, 4) for x in range(400, 561, 4))
        if spec.case == LIT_OPTICAL_TIR:
            return ((x, y) for y in range(332, 389, 4) for x in range(448, 513, 4))
        return ((x, y) for y in range(200, 521, 16) for x in range(208, 753, 12))

    @staticmethod
    def analyze_image(frame, spec):
        width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
        if (width, height) != (960, 720):
            raise SmokeFailure("optical geometry oracle requires the fixture's 960x720 camera")
        errors, actual_sum, expected_sum, reasons = [], [0.0] * 3, [0.0] * 3, {}
        channel_deviations, stable_points, rejected_edges, transmitted_chart, reentered_chart = [], 0, 0, 0, 0
        dense_crossings = []
        for x, y in ReflectionOpticalImageEvidence.reference_points(spec):
            expected, path = pixel_reference(spec.case, x + 0.5, y + 0.5, max_queries=spec.queries)
            if spec.case in DENSE_CASES:
                if path[LIT_REASON] != "opaque_dense" or path[LIT_CROSSINGS] != DENSE_RECEIVER_CROSSINGS or min(expected) <= 0.:
                    raise SmokeFailure("dense receiver oracle lacks eighteen distinct closed crossings and a positive opaque hit")
                dense_crossings.append(path[LIT_CROSSINGS])
            # Exclude a one-pixel margin around analytic discontinuities.
            neighbors = [pixel_reference(spec.case, x + dx + 0.5, y + dy + 0.5, max_queries=spec.queries)[0] for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1))]
            if any(max(abs(a - b) for a, b in zip(expected, neighbor)) > 0.025 for neighbor in neighbors):
                rejected_edges += 1
                continue
            actual = tuple(decode_radiance(value) for value in rows[y][x])
            if not all(math.isfinite(value) for value in actual):
                raise SmokeFailure("optical capture contains clipped or nonfinite decoded radiance")
            errors.extend(abs(a - b) for a, b in zip(actual, expected))
            channel_deviations.append([abs(a - b) for a, b in zip(actual, expected)])
            for channel in range(3):
                actual_sum[channel] += actual[channel]
                expected_sum[channel] += expected[channel]
            reasons[path[LIT_REASON]] = reasons.get(path[LIT_REASON], 0) + 1
            transmitted_chart += path[LIT_REASON] == LIT_CHART and path[LIT_CROSSINGS] >= 2
            reentered_chart += path[LIT_REASON] == LIT_CHART and path[LIT_CROSSINGS] >= 4
            stable_points += 1
        if stable_points < (60 if spec.case == LIT_OPTICAL_TORUS else 150):
            raise SmokeFailure("too few geometrically stable optical reference samples")
        if spec.case == LIT_OPTICAL_TORUS and (transmitted_chart < 60 or reentered_chart < 16):
            raise SmokeFailure("torus oracle lacks stable transmitted chart samples and actual re-entry coverage")
        mae = sum(errors) / len(errors)
        percentile = sorted(errors)[math.floor(0.95 * (len(errors) - 1))]
        reference_mean = sum(expected_sum) / (3 * stable_points)
        # Trim at most 5% near-critical mesh discretization outliers.
        ordered_errors = sorted(errors)
        kept_errors = ordered_errors[:max(len(ordered_errors) - math.ceil(len(ordered_errors) * 0.05), 1)]
        trimmed_mae = sum(kept_errors) / len(kept_errors)
        if trimmed_mae > 0.025 + reference_mean * 0.02 or percentile > 0.065 + reference_mean * 0.035:
            raise SmokeFailure(f"{spec.name}: reflected stripe transport disagrees with the independent reference " f"(linear RGB MAE {mae:.6f}, p95 {percentile:.6f}, expected mean {reference_mean:.6f})")
        for channel, (actual, expected) in enumerate(zip(actual_sum, expected_sum)):
            # Trim at most 5% FP32 mesh-discretization outliers near the critical angle.
            ordered = sorted(deviation[channel] for deviation in channel_deviations)
            kept = ordered[:max(stable_points - math.ceil(stable_points * 0.05), 1)]
            trimmed = sum(kept) / len(kept)
            if trimmed > 0.018 + 0.035 * expected / stable_points:
                raise SmokeFailure(spec.name + ": integrated reflected channel energy is outside its analytic bound")
        return {"stable_reference_pixels": stable_points, "omitted_discontinuity_pixels": rejected_edges, "linear_rgb_mae": mae, "linear_rgb_p95_error": percentile, "actual_mean_rgb": [value / stable_points for value in actual_sum],
            "expected_mean_rgb": [value / stable_points for value in expected_sum], "reference_termination": reasons, "transmitted_chart_samples": transmitted_chart, "reentered_chart_samples": reentered_chart,
            "dense_receiver_crossings": {"minimum": min(dense_crossings), "maximum": max(dense_crossings), "samples": len(dense_crossings)} if dense_crossings else None}

    @staticmethod
    def compare_invariant(first, second):
        if first[:2] != second[:2]:
            raise SmokeFailure("optical invariance captures have different dimensions")
        errors = [abs(a - b) for y in range(185, 535) for x in range(195, 765) for a, b in zip(first[2][y][x], second[2][y][x])]
        mae = sum(errors) / len(errors)
        changed = sum(error > 2 for error in errors)
        if mae > 0.2 or changed > len(errors) * 0.005:
            raise SmokeFailure("equivalent optical volume arrangement changed reflected transport")
        return {"mirror_byte_mae": mae, "channels_differing_by_more_than_two": changed}

