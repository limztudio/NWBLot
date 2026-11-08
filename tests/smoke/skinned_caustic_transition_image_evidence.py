#!/usr/bin/env python3
"""Compare skinned caustic transition image evidence."""

from array import array
import statistics

from window_capture_smoke import SmokeFailure

LIT_RECEIVER = "receiver"
LIT_MEAN_ABSOLUTE_CHANNEL_ERROR = "mean_absolute_channel_error"
LIT_P95_CHANNEL_ERROR = "p95_channel_error"
LIT_LARGE_ERROR_FRACTION = "large_error_fraction"
LIT_CHANGED_PIXELS = "changed_pixels"
LIT_CURRENT_VS_LAGGED = "current_vs_lagged"
LIT_REPEATED_STATES = "repeated_states"
LIT_CSG_SIGNAL = "csg_signal"
LIT_MOTION_SIGNAL = "motion_signal"
LIT_PHOTON_SIGNAL = "photon_signal"
LIT_SAMPLE = "sample"
LIT_REGION = "region"
LIT_CURRENT = "current"
LIT_GROUND = "ground"
LIT_VARIANT = "variant"
LIT_LAGGED = "lagged"
LIT_NO_PHOTONS = "no_photons"
SAMPLE_COUNT = 8
EXPECTED_EXTENT = (1280, 900)
VARIANTS = ("current", "lagged", "no_photons")
REGIONS = {"receiver": (320, 100, 960, 735), "ground": (260, 640, 1020, 870)}
MATCH_LIMITS = {"mean_absolute_channel_error": 5.0, "p95_channel_error": 12, "large_error_fraction": .03, "large_error_channel_threshold": 24}
SIGNAL_LIMITS = {"csg_changed_pixels": 256, "motion_changed_pixels": 64, "photon_changed_pixels": 32, "photon_mean_channel_error": .03, "channel_threshold": 3}


class SkinnedCausticTransitionImageEvidence:
    @staticmethod
    def frame_regions(frame):
        width, height, rows = frame
        if (width, height) != EXPECTED_EXTENT or len(rows) != height or any(len(row) != width for row in rows):
            raise SmokeFailure("transition comparisons require the actual 1280x900 framebuffer")
        regions = {}
        for name, (left, top, right, bottom) in REGIONS.items():
            values = array("B")
            for y in range(top, bottom, 2):
                for x in range(left, right, 2):
                    values.extend(rows[y][x])
            regions[name] = values
        if max(regions[LIT_RECEIVER]) - min(regions[LIT_RECEIVER]) < 12:
            raise SmokeFailure("transition receiver capture is blank or has no visible geometry contrast")
        return regions


    @staticmethod
    def difference(first, second):
        if not first or len(first) != len(second) or len(first) % 3:
            raise SmokeFailure("transition image regions must contain equal nonempty RGB samples")
        errors = [abs(left - right) for left, right in zip(first, second)]
        ordered = sorted(errors)
        changed = sum(max(errors[index:index + 3]) > SIGNAL_LIMITS["channel_threshold"] for index in range(0, len(errors), 3))
        return {LIT_MEAN_ABSOLUTE_CHANNEL_ERROR: statistics.mean(errors), LIT_P95_CHANNEL_ERROR: ordered[round((len(ordered) - 1) * .95)], LIT_LARGE_ERROR_FRACTION: sum(error > MATCH_LIMITS["large_error_channel_threshold"] for error in errors) / len(errors),
            LIT_CHANGED_PIXELS: changed, "pixels": len(errors) // 3}


    @staticmethod
    def require_match(first, second, label):
        measured = SkinnedCausticTransitionImageEvidence.difference(first, second)
        if any(measured[key] > MATCH_LIMITS[key] for key in (LIT_MEAN_ABSOLUTE_CHANNEL_ERROR, LIT_P95_CHANNEL_ERROR, LIT_LARGE_ERROR_FRACTION)):
            raise SmokeFailure(f"{label} has stale or unstable posed geometry/light: {measured}")
        return measured


    @staticmethod
    def require_signal(first, second, minimum_pixels, label, minimum_mean=0):
        measured = SkinnedCausticTransitionImageEvidence.difference(first, second)
        if measured[LIT_CHANGED_PIXELS] < minimum_pixels or measured[LIT_MEAN_ABSOLUTE_CHANNEL_ERROR] < minimum_mean:
            raise SmokeFailure(f"{label} has no qualifying visible signal: {measured}")
        return measured


    @staticmethod
    def compare_series(series):
        if set(series) != set(VARIANTS) or any(len(samples) != SAMPLE_COUNT for samples in series.values()):
            raise SmokeFailure("transition comparison requires all three complete eight-frame series")
        comparisons = {LIT_CURRENT_VS_LAGGED: [], LIT_REPEATED_STATES: [], LIT_CSG_SIGNAL: [], LIT_MOTION_SIGNAL: [], LIT_PHOTON_SIGNAL: []}
        for sample in range(SAMPLE_COUNT):
            for region in REGIONS:
                comparisons[LIT_CURRENT_VS_LAGGED].append({LIT_SAMPLE: sample, LIT_REGION: region, **SkinnedCausticTransitionImageEvidence.require_match(series[LIT_CURRENT][sample][region], series[LIT_LAGGED][sample][region], f"current/lagged sample {sample} {region}")})
            # Measure photon contribution only on ground receiver pixels.
            if sample < 2:
                comparisons[LIT_PHOTON_SIGNAL].append({LIT_SAMPLE: sample, **SkinnedCausticTransitionImageEvidence.require_signal(series[LIT_CURRENT][sample][LIT_GROUND], series[LIT_NO_PHOTONS][sample][LIT_GROUND], SIGNAL_LIMITS["photon_changed_pixels"], "hardware photons on ordinary ground",
                        SIGNAL_LIMITS["photon_mean_channel_error"])})
        for variant in (LIT_CURRENT, LIT_LAGGED):
            for sample in range(4):
                for region in REGIONS:
                    comparisons[LIT_REPEATED_STATES].append({LIT_VARIANT: variant, LIT_SAMPLE: sample, LIT_REGION: region, **SkinnedCausticTransitionImageEvidence.require_match(series[variant][sample][region], series[variant][sample + 4][region], f"{variant} repeated ordinary/CSG pose {sample} {region}")})
            for sample in range(2):
                comparisons[LIT_CSG_SIGNAL].append({LIT_VARIANT: variant, LIT_SAMPLE: sample, **SkinnedCausticTransitionImageEvidence.require_signal(series[variant][sample][LIT_RECEIVER], series[variant][sample + 2][LIT_RECEIVER],
                        SIGNAL_LIMITS["csg_changed_pixels"], "active cutter on the live skinned receiver")})
            comparisons[LIT_MOTION_SIGNAL].append({LIT_VARIANT: variant, **SkinnedCausticTransitionImageEvidence.require_signal(series[variant][0][LIT_RECEIVER], series[variant][1][LIT_RECEIVER], SIGNAL_LIMITS["motion_changed_pixels"], "live joint pose motion")})
        return comparisons


