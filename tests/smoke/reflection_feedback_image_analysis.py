"""Analyze feedback capture geometry and image equivalence."""

import math

from reflection_image_analysis import ReflectionImageAnalysis, FOREGROUND_REGION, GLASS_REGION, OPAQUE_REGION
from reflection_roughness_image_evidence import ReflectionRoughnessImageEvidence
from reflection_smoke import SmokeFailure

LIT_HYBRID = "hybrid"
LIT_FLOOR = "floor"
LIT_ONSCREEN = "onscreen"
LIT_OPAQUE_GLASS = "opaque_glass"
LIT_ROUGH_FURNACE = "rough_furnace"
LIT_FEEDBACK_LONG_MISS = "feedback_long_miss"
LIT_RED = "red"
LIT_GREEN = "green"
LIT_DIRECT = "direct"
LIT_REFLECTION = "reflection"
LIT_PIXELS = "pixels"
LIT_CENTROID = "centroid"
LIT_BASELINE_2 = "baseline"
LIT_FEEDBACK_2 = "feedback"


class ReflectionFeedbackImageAnalysis:
    @staticmethod
    def long_panel_projection(width, height, color, reflected):
        focal = height / (2.0 * math.tan(math.pi / 6.0))
        x = -1.7 if color == LIT_RED else 1.7
        y = -3.6 if reflected else 3.6
        return (width * 0.5 + focal * x / 12.0, height * 0.5 - focal * (y - 1.4) / 12.0, focal * 0.3 / 12.0, focal * 0.35 / 12.0)

    @staticmethod
    def analyze_long_miss_panels(frame):
        width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
        results = {}
        for color in (LIT_RED, LIT_GREEN):
            points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if ReflectionImageAnalysis.marker_color(pixel, color)]
            projections = [ReflectionFeedbackImageAnalysis.long_panel_projection(width, height, color, reflected) for reflected in (False, True)]
            color_result = {}
            for label, projection in zip((LIT_DIRECT, LIT_REFLECTION), projections):
                selected = [point for point in points if ReflectionImageAnalysis.in_panel_rectangle(point, projection, height * 0.008)]
                minimum = max(32, projection[2] * projection[3])
                if len(selected) < minimum:
                    raise SmokeFailure(f"long SSR scene is missing the geometrically predicted {color} {label} panel")
                centroid = (sum(x for x, _ in selected) / len(selected), sum(y for _, y in selected) / len(selected))
                if math.hypot(centroid[0] - projection[0], centroid[1] - projection[1]) > height * 0.012:
                    raise SmokeFailure(f"long SSR {color} {label} centroid is outside its projected position")
                color_result[label] = {LIT_PIXELS: len(selected), "minimum_projected_area_pixels": math.ceil(minimum), LIT_CENTROID: centroid, "expected_centroid": projection[:2]}
            outside = sum(not any(ReflectionImageAnalysis.in_panel_rectangle(point, projection, height * 0.008) for projection in projections) for point in points)
            if outside > max(16, len(points) * 0.03):
                raise SmokeFailure(f"long SSR {color} pixels appeared outside the actual/direct reflection geometry")
            color_result["outside_predicted_regions"] = outside
            color_result["projected_traversal_pixels"] = abs(projections[0][1] - projections[1][1])
            results[color] = color_result
        return results

    @staticmethod
    def compare_static_images(baseline, feedback, case):
        if case == LIT_ROUGH_FURNACE:
            # Average pixel samples across each rough frame instead of comparing stochastic pixels directly.
            from reflection_roughness_reference import analyze_furnace
            return {LIT_BASELINE_2: analyze_furnace(baseline, 1.0), LIT_FEEDBACK_2: analyze_furnace(feedback, 1.0)}
        if case == LIT_FEEDBACK_LONG_MISS:
            before, after = ReflectionFeedbackImageAnalysis.analyze_long_miss_panels(baseline), ReflectionFeedbackImageAnalysis.analyze_long_miss_panels(feedback)
        elif case == LIT_OPAQUE_GLASS:
            results = []
            for frame in (baseline, feedback):
                width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
                counts = {name: sum(ReflectionImageAnalysis.marker_color(rows[y][x], LIT_RED) or ReflectionImageAnalysis.marker_color(rows[y][x], LIT_GREEN) for x, y in ReflectionImageAnalysis.region_pixels(width, height, region)) for name, region in (("opaque", OPAQUE_REGION), ("glass", GLASS_REGION))}
                counts["foreground"] = sum(ReflectionImageAnalysis.blue_foreground(rows[y][x]) for x, y in ReflectionImageAnalysis.region_pixels(width, height, FOREGROUND_REGION))
                if min(counts.values()) < 100:
                    raise SmokeFailure("feedback opaque/glass composition lost a reflected region or its foreground control")
                results.append(counts)
            before, after = results
        elif case in (LIT_FLOOR, LIT_ONSCREEN):
            before, after = ReflectionImageAnalysis.analyze_panels(baseline, case, LIT_HYBRID), ReflectionImageAnalysis.analyze_panels(feedback, case, LIT_HYBRID)
        else:
            before, after = ReflectionImageAnalysis.analyze_markers(baseline), ReflectionImageAnalysis.analyze_markers(feedback)
        return {LIT_BASELINE_2: before, LIT_FEEDBACK_2: after, "image_equivalence": ReflectionRoughnessImageEvidence.compare_exact_scene(baseline, feedback)}

    @staticmethod
    def compare_mutation_images(initial, changed, fresh):
        before = ReflectionImageAnalysis.analyze_panels(initial, "boundary", LIT_HYBRID)
        after = ReflectionImageAnalysis.analyze_panels(changed, LIT_ONSCREEN, LIT_HYBRID)
        fresh_result = ReflectionImageAnalysis.analyze_panels(fresh, LIT_ONSCREEN, LIT_HYBRID)
        comparison = ReflectionRoughnessImageEvidence.compare_exact_scene(changed, fresh)
        expected_shift = initial[1] / (2.0 * math.tan(math.pi / 6.0)) * (1.7 - 3.0) / 9.0
        measured_shift = after[LIT_GREEN][LIT_REFLECTION][LIT_CENTROID][0] - before[LIT_GREEN][LIT_REFLECTION][LIT_CENTROID][0]
        if abs(measured_shift - expected_shift) > initial[1] * 0.012:
            raise SmokeFailure("feedback mutation retained the old green reflection instead of its moved geometry")
        for label in (LIT_DIRECT, LIT_REFLECTION):
            first, last = before[LIT_RED][label], after[LIT_RED][label]
            if abs(first[LIT_PIXELS] - last[LIT_PIXELS]) > max(4, first[LIT_PIXELS] * 0.01):
                raise SmokeFailure("feedback mutation changed the stationary red control")
        return {"initial": before, "changed": after, "fresh": fresh_result, "changed_equals_fresh": comparison, "expected_green_motion_pixels": expected_shift, "measured_green_motion_pixels": measured_shift}
