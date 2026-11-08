"""Geometry and color checks for reflection smoke captures."""

import math

from reflection_statistics import LIT_DISABLED, LIT_FLOOR, LIT_SCREEN
from window_capture_smoke import SmokeFailure

LIT_MOVED = "moved"
LIT_ONSCREEN = "onscreen"
LIT_ONSCREEN_MOVED = "onscreen_moved"
LIT_BOUNDARY = "boundary"
LIT_RED = "red"
LIT_GREEN = "green"
LIT_PIXELS = "pixels"
LIT_MINIMUM_PROJECTED_AREA_PIXELS = "minimum_projected_area_pixels"
LIT_CENTROID = "centroid"
LIT_EXPECTED_CENTROID = "expected_centroid"
LIT_MEASURED_MOTION = "measured_motion"
LIT_EXPECTED_MOTION_X = "expected_motion_x"
LIT_ORIGINAL = "original"
LIT_DIRECT = "direct"
LIT_REFLECTION = "reflection"
LIT_EXPECTED = "expected"
OPAQUE_REGION = (0.28, 0.36, 0.43, 0.57)
GLASS_REGION = (0.57, 0.36, 0.72, 0.57)
FOREGROUND_REGION = (0.21, 0.59, 0.79, 0.65)
EXTERIOR_REGIONS = ((0.02, 0.05, 0.20, 0.95), (0.80, 0.05, 0.98, 0.95), (0.2, 0.05, 0.8, 0.24))


class ReflectionImageAnalysis:
    @staticmethod
    def validate_frame(frame):
        width, height, rows = frame
        if width < 320 or height < 240 or len(rows) != height or any(len(row) != width for row in rows):
            raise SmokeFailure("reflection framebuffer is malformed or too small")
        return width, height, rows


    @staticmethod
    def region_pixels(width, height, region):
        left, top, right, bottom = region
        for y in range(int(top * height), int(bottom * height)):
            for x in range(int(left * width), int(right * width)):
                yield x, y


    @staticmethod
    def marker_color(pixel, color):
        channel = 0 if color == LIT_RED else 1
        return pixel[channel] >= 40 and all(pixel[channel] >= pixel[other] + 25 for other in range(3) if other != channel)


    @staticmethod
    def blue_foreground(pixel):
        return pixel[2] >= 110 and pixel[2] >= pixel[0] + 45 and pixel[2] >= pixel[1] + 45


    @staticmethod
    def marker_projection(width, height, color, moved=False):
        # Virtual source reflected through z=0 sits 14 units from the camera at z=-6.
        focal_length = height / (2.0 * math.tan(math.pi / 6.0))
        x, y = (-1.6, 1.0) if color == LIT_RED else (1.6, 2.0)
        x += 0.9 if moved else 0.0
        return width * 0.5 + focal_length * x / 14.0, height * 0.5 - focal_length * (y - 1.4) / 14.0


    @staticmethod
    def analyze_markers(frame, moved=False, required=True):
        width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
        results = {}
        for color in (LIT_RED, LIT_GREEN):
            points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if ReflectionImageAnalysis.marker_color(pixel, color)]
            if not required:
                if len(points) > 16:
                    raise SmokeFailure(f"offscreen {color} marker appeared without a hardware reflection hit ({len(points)} pixels)")
                results[color] = {LIT_PIXELS: len(points)}
                continue
            projected_radius = height / (2.0 * math.tan(math.pi / 6.0)) * 0.65 / 14.0
            minimum_pixels = max(80, math.pi * projected_radius ** 2 * 0.25)
            if len(points) < minimum_pixels:
                raise SmokeFailure(f"hardware reflection is missing the offscreen {color} marker " f"({len(points)} pixels, at least {math.ceil(minimum_pixels)} required by its projected area)")
            expected_x, expected_y = ReflectionImageAnalysis.marker_projection(width, height, color, moved)
            radius = projected_radius + height * 0.015
            outside = sum((x - expected_x) ** 2 + (y - expected_y) ** 2 > radius ** 2 for x, y in points)
            centroid_x = sum(x for x, _ in points) / len(points)
            centroid_y = sum(y for _, y in points) / len(points)
            if outside > max(16, len(points) * 0.05):
                raise SmokeFailure(f"{color} reflection is outside its geometrically predicted marker region ({outside}/{len(points)} pixels)")
            if math.hypot(centroid_x - expected_x, centroid_y - expected_y) > height * 0.025:
                raise SmokeFailure(f"{color} reflection centroid does not match the virtual marker position")
            results[color] = {LIT_PIXELS: len(points), LIT_MINIMUM_PROJECTED_AREA_PIXELS: math.ceil(minimum_pixels), LIT_CENTROID: [centroid_x, centroid_y], LIT_EXPECTED_CENTROID: [expected_x, expected_y], "outside_predicted_region": outside}
        return results


    @staticmethod
    def compare_marker_motion(original, moved):
        if original[:2] != moved[:2]:
            raise SmokeFailure("marker motion captures have different framebuffer dimensions")
        before = ReflectionImageAnalysis.analyze_markers(original)
        after = ReflectionImageAnalysis.analyze_markers(moved, moved=True)
        expected_shift = original[1] / (2.0 * math.tan(math.pi / 6.0)) * 0.9 / 14.0
        for color in (LIT_RED, LIT_GREEN):
            dx = after[color][LIT_CENTROID][0] - before[color][LIT_CENTROID][0]
            dy = after[color][LIT_CENTROID][1] - before[color][LIT_CENTROID][1]
            if abs(dx - expected_shift) > original[1] * 0.014 or abs(dy) > original[1] * 0.014:
                raise SmokeFailure(f"{color} reflection did not follow the authored marker movement")
            after[color][LIT_MEASURED_MOTION] = [dx, dy]
            after[color][LIT_EXPECTED_MOTION_X] = expected_shift
        return {LIT_ORIGINAL: before, LIT_MOVED: after}


    @staticmethod
    def panel_projection(width, height, color, case, reflected):
        focal_length = height / (2.0 * math.tan(math.pi / 6.0))
        source_x, source_y, half_height = (-1.7, 1.0, 0.25) if color == LIT_RED else (1.7, 2.0, 0.35)
        if case == LIT_ONSCREEN_MOVED:
            source_x += 0.25 if color == LIT_RED else -0.25
        elif case == LIT_BOUNDARY and color == LIT_GREEN:
            source_x = 3.0
        # Virtual image at z=+3 sits three times farther from the camera than the source at z=-3.
        distance = 9.0 if reflected else 3.0
        if case == LIT_FLOOR:
            source_y = -2.0 if reflected else 2.0
            distance = 9.0
        return (width * 0.5 + focal_length * source_x / distance, height * 0.5 - focal_length * (source_y - 1.4) / distance, focal_length * 0.3 / distance, focal_length * half_height / distance)


    @staticmethod
    def in_panel_rectangle(point, projection, margin):
        x, y = point
        cx, cy, half_width, half_height = projection
        return abs(x - cx) <= half_width + margin and abs(y - cy) <= half_height + margin


    @staticmethod
    def analyze_panels(frame, case, mode):
        width, height, rows = ReflectionImageAnalysis.validate_frame(frame)
        results = {}
        for color in (LIT_RED, LIT_GREEN):
            points = [(x, y) for y, row in enumerate(rows) for x, pixel in enumerate(row) if ReflectionImageAnalysis.marker_color(pixel, color)]
            allowed_regions = []
            color_results = {}
            for label, reflected in ((LIT_DIRECT, False), (LIT_REFLECTION, True)):
                expected = not (case == LIT_BOUNDARY and color == LIT_GREEN) if not reflected else (mode != LIT_DISABLED and not (case == LIT_BOUNDARY and color == LIT_GREEN and mode == LIT_SCREEN))
                projection = ReflectionImageAnalysis.panel_projection(width, height, color, case, reflected)
                region_points = [point for point in points if ReflectionImageAnalysis.in_panel_rectangle(point, projection, height * 0.008)]
                if not expected:
                    if len(region_points) > 16:
                        raise SmokeFailure(f"{case}/{mode}: unexpected {color} {label} panel ({len(region_points)} pixels)")
                    color_results[label] = {LIT_PIXELS: len(region_points), LIT_EXPECTED: False}
                    continue
                minimum = max(32, 4.0 * projection[2] * projection[3] * (0.25 if reflected else 0.5))
                if len(region_points) < minimum:
                    raise SmokeFailure(f"{case}/{mode}: missing {color} {label} panel " f"({len(region_points)} pixels; projected area requires {math.ceil(minimum)})")
                cx = sum(x for x, _ in region_points) / len(region_points)
                cy = sum(y for _, y in region_points) / len(region_points)
                if math.hypot(cx - projection[0], cy - projection[1]) > max(2.0, height * 0.012):
                    raise SmokeFailure(f"{case}/{mode}: {color} {label} centroid does not match its geometric projection")
                allowed_regions.append(projection)
                color_results[label] = {LIT_PIXELS: len(region_points), LIT_EXPECTED: True, LIT_CENTROID: [cx, cy], LIT_EXPECTED_CENTROID: list(projection[:2]), LIT_MINIMUM_PROJECTED_AREA_PIXELS: math.ceil(minimum)}
            outside = sum(not any(ReflectionImageAnalysis.in_panel_rectangle(point, region, height * 0.008) for region in allowed_regions) for point in points)
            if outside > max(16, len(points) * 0.03):
                raise SmokeFailure(f"{case}/{mode}: {color} pixels appeared outside direct/reflected panel regions ({outside})")
            color_results["outside_predicted_regions"] = outside
            results[color] = color_results
        return results


    @staticmethod
    def compare_panel_motion(original, moved):
        if original[:2] != moved[:2]:
            raise SmokeFailure("panel motion captures have different framebuffer dimensions")
        before = ReflectionImageAnalysis.analyze_panels(original, LIT_ONSCREEN, LIT_SCREEN)
        after = ReflectionImageAnalysis.analyze_panels(moved, LIT_ONSCREEN_MOVED, LIT_SCREEN)
        focal_length = original[1] / (2.0 * math.tan(math.pi / 6.0))
        for color in (LIT_RED, LIT_GREEN):
            direction = 1.0 if color == LIT_RED else -1.0
            for label, distance in ((LIT_DIRECT, 3.0), (LIT_REFLECTION, 9.0)):
                expected_dx = focal_length * direction * 0.25 / distance
                dx = after[color][label][LIT_CENTROID][0] - before[color][label][LIT_CENTROID][0]
                dy = after[color][label][LIT_CENTROID][1] - before[color][label][LIT_CENTROID][1]
                if abs(dx - expected_dx) > max(1.5, original[1] * 0.006) or abs(dy) > max(1.5, original[1] * 0.006):
                    raise SmokeFailure(f"{color} {label} panel did not follow the authored inward movement")
                after[color][label][LIT_MEASURED_MOTION] = [dx, dy]
                after[color][label][LIT_EXPECTED_MOTION_X] = expected_dx
        return {LIT_ORIGINAL: before, LIT_MOVED: after}


    @staticmethod
    def compare_opaque_glass(reference, reflected):
        width, height, before = ReflectionImageAnalysis.validate_frame(reference)
        ReflectionImageAnalysis.validate_frame(reflected)
        if reference[:2] != reflected[:2]:
            raise SmokeFailure("opaque/glass captures have different framebuffer dimensions")
        after = reflected[2]
        metrics = {}
        for name, region in (("opaque", OPAQUE_REGION), ("glass", GLASS_REGION)):
            added = 0
            reference_colored = 0
            region_count = 0
            for x, y in ReflectionImageAnalysis.region_pixels(width, height, region):
                region_count += 1
                old_color = ReflectionImageAnalysis.marker_color(before[y][x], LIT_RED) or ReflectionImageAnalysis.marker_color(before[y][x], LIT_GREEN)
                new_color = ReflectionImageAnalysis.marker_color(after[y][x], LIT_RED) or ReflectionImageAnalysis.marker_color(after[y][x], LIT_GREEN)
                reference_colored += old_color
                added += new_color and not old_color
            if reference_colored > 16 or added < max(48, region_count * 0.02):
                raise SmokeFailure(f"{name} sphere did not acquire localized offscreen reflection colors ({added} added pixels)")
            metrics[name + "_new_reflected_pixels"] = added
        foreground = [(x, y) for x, y in ReflectionImageAnalysis.region_pixels(width, height, FOREGROUND_REGION) if ReflectionImageAnalysis.blue_foreground(before[y][x])]
        retained = sum(ReflectionImageAnalysis.blue_foreground(after[y][x]) for x, y in foreground)
        added_foreground = sum(ReflectionImageAnalysis.blue_foreground(after[y][x]) and not ReflectionImageAnalysis.blue_foreground(before[y][x]) for x, y in ReflectionImageAnalysis.region_pixels(width, height, FOREGROUND_REGION))
        if len(foreground) < width * height * 0.002 or retained < len(foreground) * 0.97 or added_foreground > len(foreground) * 0.03:
            raise SmokeFailure("reflection displaced, expanded, or removed the foreground AVBOIT strip")
        exterior_count = 0
        exterior_changed = 0
        for region in EXTERIOR_REGIONS:
            for x, y in ReflectionImageAnalysis.region_pixels(width, height, region):
                exterior_count += 1
                exterior_changed += max(abs(a - b) for a, b in zip(before[y][x], after[y][x])) > 8
        if exterior_changed > max(16, exterior_count * 0.005):
            raise SmokeFailure("reflection changed pixels outside the opaque/glass silhouettes")
        metrics.update(foreground_pixels=len(foreground), foreground_retained=retained, foreground_added=added_foreground, exterior_changed=exterior_changed)
        return metrics

