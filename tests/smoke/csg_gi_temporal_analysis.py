"""Analyze temporal CSG captures and apply their evidence gates."""

import hashlib
import statistics
from array import array

from window_capture_image import read_bmp_24_rows
from csg_gi_temporal_schedule import CsgTemporalSchedule
from window_capture_smoke import SmokeFailure
from csg_gi_temporal_contract import (
    ANIMATED_INTERIOR_REGIONS,
    CAP_STATE_PROBES,
    EXPECTED_EXTENT,
    FRAME_INTERVAL,
    GEOMETRY_LIMITS,
    GEOMETRY_PROBES,
    LIT_ANIMATED,
    LIT_CHANNEL_JUMP,
    LIT_COMPONENT_TOLERANCE,
    LIT_DECODED,
    LIT_DIAGNOSTIC_POINTS,
    LIT_DISPLAY_RGB,
    LIT_EVENT_DATA,
    LIT_EVENT_ORDER,
    LIT_EVENT_STATE,
    LIT_FAILED_SPAN_FLAGS,
    LIT_FIRST_FRONT,
    LIT_FRAMES,
    LIT_GRAPHICS_FRAME,
    LIT_HORIZONTAL_JUMP_FRACTION,
    LIT_LAGGED_LIGHTING,
    LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B,
    LIT_MEAN_RGB,
    LIT_MOTION,
    LIT_NATURAL,
    LIT_NORMAL,
    LIT_OVERLAPPING,
    LIT_P95_TEMPORAL_RANGE,
    LIT_PARTIAL_SUFFIX,
    LIT_PIXEL,
    LIT_POSITION,
    LIT_REGIONS,
    LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH,
    LIT_ROUTE,
    LIT_RUNTIME_LOG,
    LIT_SAMPLES,
    LIT_SECOND_FRONT,
    LIT_SOFTWARE,
    LIT_SPAN_PRESENT,
    LIT_SPAN_STATE,
    LIT_STATE,
    LIT_STATIC,
    LIT_UTF_8,
    LIT_VERTICAL_JUMP_FRACTION,
    LIT_VIEW,
    QUALITY_LIMITS,
    REGIONS,
    SAMPLE_COUNT,
    STATE_CHANNELS,
    STATIC_EXTERIOR_REGIONS,
)

class CsgTemporalAnalysis:
    @staticmethod
    def sample_path(directory, sample):
        return directory / ("series.bmp" if sample + 1 == SAMPLE_COUNT else f"series.bmp.{sample}.bmp")

    @staticmethod
    def percentile(values, fraction):
        ordered = sorted(values)
        return ordered[round((len(ordered) - 1) * fraction)]

    @staticmethod
    def measure_region_frame(rows, region):
        center_x, center_y, half_width, half_height = region
        total = [0, 0, 0]
        horizontal_jumps = vertical_jumps = 0
        for y in range(center_y - half_height, center_y + half_height):
            for x in range(center_x - half_width, center_x + half_width):
                for channel, value in enumerate(rows[y][x]):
                    total[channel] += value
                    if x > center_x - half_width:
                        horizontal_jumps += abs(value - rows[y][x - 1][channel]) > QUALITY_LIMITS[LIT_CHANNEL_JUMP]
                    if y > center_y - half_height:
                        vertical_jumps += abs(value - rows[y - 1][x][channel]) > QUALITY_LIMITS[LIT_CHANNEL_JUMP]
        return {LIT_MEAN_RGB: [value / (4 * half_width * half_height) for value in total], LIT_HORIZONTAL_JUMP_FRACTION: horizontal_jumps / ((2 * half_width - 1) * 2 * half_height * 3),
            LIT_VERTICAL_JUMP_FRACTION: vertical_jumps / ((2 * half_height - 1) * 2 * half_width * 3)}

    @staticmethod
    def decode_diagnostic(rgb, view):
        display_linear = [value / 255 / 12.92 if value / 255 <= .04045 else ((value / 255 + .055) / 1.055) ** 2.4 for value in rgb]
        # The fixture preserves the renderer's exposure=1, Reinhard shoulder=0.65 presentation defaults.
        if any(value >= 1 for value in display_linear):
            raise SmokeFailure("diagnostic color cannot invert saturated Reinhard presentation")
        encoded = [.65 * value / (1 - value) for value in display_linear]
        if view in STATE_CHANNELS:
            return encoded
        return [value * scale - offset for value, scale, offset in zip(encoded, (2, 2, 2) if view == LIT_NORMAL else (6, 6, 2), (1, 1, 1) if view == LIT_NORMAL else (3, 3, 1))]

    @staticmethod
    def position_quantization_tolerance(rgb, decoded):
        lower = CsgTemporalAnalysis.decode_diagnostic([max(value - .5, 0) for value in rgb], LIT_POSITION)
        upper = CsgTemporalAnalysis.decode_diagnostic([min(value + .5, 254.5) for value in rgb], LIT_POSITION)
        return [min(GEOMETRY_LIMITS["position_component_max"], max(value - low, high - value) + GEOMETRY_LIMITS["position_arithmetic_allowance"]) for value, low, high in zip(decoded, lower, upper)]

    @staticmethod
    def measure_series(directory, frame_interval=FRAME_INTERVAL):
        log = (directory / LIT_RUNTIME_LOG).read_text(encoding=LIT_UTF_8)
        animated, view, overlap_count, warmup_start_frame, warmup_elapsed_seconds, first_frame, captured, bounded_motion = CsgTemporalSchedule.parse(log, frame_interval)
        region_bounds = dict(REGIONS)
        if animated == "0":
            region_bounds.update(STATIC_EXTERIOR_REGIONS)
        else:
            # The static ceiling band crosses real pitched ceiling/wall edges during motion.
            region_bounds.pop("box_upper_band")
            region_bounds.update(ANIMATED_INTERIOR_REGIONS)
        regions = {name: [] for name in region_bounds}
        files = []
        for index, frame in captured:
            path = CsgTemporalAnalysis.sample_path(directory, index)
            if path.with_name(path.name + LIT_PARTIAL_SUFFIX).exists():
                raise SmokeFailure("temporal series left an incomplete capture")
            width, height, rows = read_bmp_24_rows(path)
            if (width, height) != EXPECTED_EXTENT:
                raise SmokeFailure("temporal series must retain the actual 1280x900 framebuffer")
            for name, (center_x, center_y, half_width, half_height) in region_bounds.items():
                samples = array("B")
                for y in range(center_y - half_height, center_y + half_height, 2):
                    for x in range(center_x - half_width, center_x + half_width, 2):
                        samples.extend(rows[y][x])
                regions[name].append(samples)
            files.append({"sample": index, LIT_GRAPHICS_FRAME: frame, "file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), LIT_REGIONS: {name: CsgTemporalAnalysis.measure_region_frame(rows, region) for name, region in region_bounds.items()}})
            if view in (LIT_NORMAL, LIT_POSITION):
                points = {}
                for name, probe in GEOMETRY_PROBES.items():
                    x, y = probe[LIT_PIXEL]
                    rgb = rows[y][x]
                    decoded = CsgTemporalAnalysis.decode_diagnostic(rgb, view)
                    points[name] = {LIT_PIXEL: [x, y], LIT_DISPLAY_RGB: list(rgb), LIT_DECODED: decoded}
                    if view == LIT_POSITION:
                        points[name][LIT_COMPONENT_TOLERANCE] = CsgTemporalAnalysis.position_quantization_tolerance(rgb, decoded)
                files[-1][LIT_DIAGNOSTIC_POINTS] = points
            elif view in STATE_CHANNELS:
                points = {}
                for name, (x, y) in CAP_STATE_PROBES.items():
                    rgb = rows[y][x]
                    decoded = CsgTemporalAnalysis.decode_diagnostic(rgb, view)
                    points[name] = {LIT_PIXEL: [x, y], LIT_DISPLAY_RGB: list(rgb), LIT_DECODED: decoded}
                    if view in (LIT_EVENT_STATE, LIT_SPAN_STATE):
                        points[name].update({label: round(value * 33) for label, value in zip(STATE_CHANNELS[view], decoded)})
                        if view == LIT_EVENT_STATE:
                            points[name]["raw_count_overflow"] = points[name]["raw_count"] >= 33
                    elif view == LIT_EVENT_DATA:
                        points[name].update({label: value > .5 for label, value in zip(STATE_CHANNELS[view], decoded)})
                        if not points[name][LIT_SPAN_PRESENT]:
                            points[name][LIT_FAILED_SPAN_FLAGS] = round(decoded[0] * 33)
                    elif view == LIT_EVENT_ORDER:
                        valid_pair = decoded[1] > .25 and decoded[2] > .25
                        points[name].update({LIT_SPAN_PRESENT: decoded[0] > .5, "validated_pair": valid_pair, LIT_FIRST_FRONT: decoded[1] > .75 if valid_pair else None, LIT_SECOND_FRONT: decoded[2] > .75 if valid_pair else None})
                        if not points[name][LIT_SPAN_PRESENT]:
                            points[name][LIT_FAILED_SPAN_FLAGS] = round(decoded[0] * 33)
                    else:
                        points[name][LIT_STATE] = {label: value > .5 for label, value in zip(STATE_CHANNELS[view], decoded)}
                files[-1][LIT_DIAGNOSTIC_POINTS] = points
        metrics = {}
        for name, frames in regions.items():
            ranges = []
            deviations = []
            changes = []
            horizontal_changes = []
            row_width = region_bounds[name][2]
            for pixel in range(len(frames[0])):
                values = [frame[pixel] for frame in frames]
                ranges.append(max(values) - min(values))
                deviations.append(statistics.pstdev(values))
                changes.extend(abs(current - previous) for previous, current in zip(values, values[1:]))
            for frame in frames:
                for pixel in range(len(frame) // 3):
                    if pixel % row_width:
                        horizontal_changes.extend(abs(frame[pixel * 3 + channel] - frame[(pixel - 1) * 3 + channel]) for channel in range(3))
            metrics[name] = {"sampled_pixels": len(frames[0]) // 3, LIT_MEAN_RGB: [statistics.fmean(value for frame in frames for value in frame[channel::3]) for channel in range(3)],
                "mean_temporal_range": statistics.fmean(ranges), LIT_P95_TEMPORAL_RANGE: CsgTemporalAnalysis.percentile(ranges, .95), "max_temporal_range": max(ranges), "mean_temporal_standard_deviation": statistics.fmean(deviations),
                "mean_adjacent_change": statistics.fmean(changes), "p95_adjacent_change": CsgTemporalAnalysis.percentile(changes, .95), "max_adjacent_change": max(changes), "mean_horizontal_change": statistics.fmean(horizontal_changes),
                "p95_horizontal_change": CsgTemporalAnalysis.percentile(horizontal_changes, .95), "large_horizontal_change_fraction": sum(change > QUALITY_LIMITS[LIT_CHANNEL_JUMP] for change in horizontal_changes) / len(horizontal_changes),
                "max_frame_horizontal_jump_fraction": max(frame[LIT_REGIONS][name][LIT_HORIZONTAL_JUMP_FRACTION] for frame in files), "max_frame_vertical_jump_fraction": max(frame[LIT_REGIONS][name][LIT_VERTICAL_JUMP_FRACTION] for frame in files),
                "min_frame_mean_rgb": [min(frame[LIT_REGIONS][name][LIT_MEAN_RGB][channel] for frame in files) for channel in range(3)]}
            if animated == "1" and bounded_motion:
                peers = {}
                pairs = []
                for sample, graphics_frame in captured:
                    phase = graphics_frame % 240
                    pose_tick = min(phase, 240 - phase)
                    for previous in peers.get(pose_tick, ()):
                        differences = [abs(current - prior) for current, prior in zip(frames[sample], frames[previous])]
                        pairs.append({LIT_SAMPLES: [previous, sample], "graphics_frames": [captured[previous][1], graphics_frame], "pose_tick": pose_tick, "mean_difference": statistics.fmean(differences), "p95_difference": CsgTemporalAnalysis.percentile(differences, .95),
                            "max_difference": max(differences)})
                    peers.setdefault(pose_tick, []).append(sample)
                metrics[name]["equal_pose_differences"] = pairs
        return {LIT_FRAMES: files, LIT_REGIONS: metrics, "region_bounds": region_bounds, "dimensions": list(EXPECTED_EXTENT), LIT_VIEW: view, LIT_MOTION: LIT_ANIMATED if animated == "1" else LIT_STATIC, "frame_interval": frame_interval,
            "warmup_start_graphics_frame": warmup_start_frame, "warmup_elapsed_seconds": warmup_elapsed_seconds, "first_capture_graphics_frame": first_frame, LIT_OVERLAPPING: bool(overlap_count),
            LIT_ROUTE: LIT_SOFTWARE if LIT_LOADER_HARDWARE_RAY_TRACING_DISABLED_B in log else LIT_NATURAL, LIT_LAGGED_LIGHTING: LIT_RENDERERSYSTEM_FRAME_LAGGED_ASYNC_LIGH in log,
            "pose_contract": ("Static initial phase; unchanged camera and light." if animated == "0" else "Front-facing triangle-wave time min(frame%240,240-frame%240)*.3/120; unchanged camera and light."
                if bounded_motion else "Unidentified archived animated pose; equal-pose comparison omitted.")}




