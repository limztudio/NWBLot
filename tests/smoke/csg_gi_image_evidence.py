"""Project and validate the captured CSG global-illumination image evidence."""
import math
import statistics

from window_capture_smoke import SmokeFailure

LIT_PASSAGE = "passage"
LIT_CAP_BOUNCE = "cap_bounce"
LIT_CAP_SURFACE = "cap_surface"
LIT_CONTROL = "control"
LIT_MIXED_PASSAGE = "mixed_passage"
LIT_MIXED_FLOOR = "mixed_floor"
LIT_MIXED_OPEN_SURFACE = "mixed_open_surface"
LIT_MIXED_OPEN_SURFACE_RIGHT = "mixed_open_surface_right"
LIT_SAMPLES = "samples"
LIT_MEAN_RGB = "mean_rgb"
LIT_BLUE_EXCESS = "blue_excess"
LIT_GREEN_EXCESS = "green_excess"
LIT_VISIBLE_PASSAGE = "visible_passage"
LIT_VISIBLE_CAP = "visible_cap"
LIT_COVERED_SAMPLES = "covered_samples"
LIT_COVERAGE = "coverage"
LIT_SIGNAL_THRESHOLD = "signal_threshold"
LIT_MINIMUM_SIGNAL = "minimum_signal"
LIT_MEAN_SIGNAL = "mean_signal"
LIT_VISIBLE_MIXED_PASSAGE = "visible_mixed_passage"
LIT_REFERENCE = "reference"
LIT_CUT = "cut"
ARMS = ("reference", "cut", "uncut")
EXPECTED_EXTENT = (1280, 900)


class CsgGiImageEvidence:
    @staticmethod
    def project_point(x, y, z):
        # Static camera at x=0, y=1.3, z=-5.5 with a 60-degree vertical FOV.
        scale = EXPECTED_EXTENT[1] / (2.0 * math.tan(math.pi / 6.0) * (z + 5.5))
        return round(EXPECTED_EXTENT[0] * 0.5 + x * scale - 0.5), round(EXPECTED_EXTENT[1] * 0.5 - (y - 1.3) * scale - 0.5)

    @staticmethod
    def measure_frame(frame):
        _, _, rows = frame
        regions = { LIT_PASSAGE: {CsgGiImageEvidence.project_point(-1.3 + ix * 0.05, 1.3 + iy * 0.05, 1.0) for iy in range(-4, 5) for ix in range(-4, 5)}, LIT_CAP_BOUNCE: {CsgGiImageEvidence.project_point(1.3 + ix * 0.05, 0.61, -0.32 + iz * 0.035) for iz in range(-4, 5) for ix in range(-4, 5)},
            LIT_CAP_SURFACE: {CsgGiImageEvidence.project_point(1.3 + ix * 0.05, 1.3 + iy * 0.05, 0.4) for iy in range(-4, 5) for ix in range(-4, 5)}, LIT_CONTROL: {CsgGiImageEvidence.project_point(ix * 0.035, 2.92 + iy * 0.025, 0.0) for iy in range(-4, 5) for ix in range(-4, 5)},
            LIT_MIXED_PASSAGE: {CsgGiImageEvidence.project_point(ix * 0.03, -0.55 + iy * 0.025, 1.0) for iy in range(-4, 5) for ix in range(-4, 5)}, LIT_MIXED_FLOOR: {CsgGiImageEvidence.project_point(-1.025 + ix * 0.03125, -0.77, -1.475 + iz * 0.04375) for iz in range(-4, 5) for ix in range(-4, 5)},
            LIT_MIXED_OPEN_SURFACE: {CsgGiImageEvidence.project_point(-0.95 + ix * 0.025, -0.3 + iy * 0.025, -1.0) for iy in range(-4, 5) for ix in range(-4, 5)}, LIT_MIXED_OPEN_SURFACE_RIGHT: {CsgGiImageEvidence.project_point(0.95 + ix * 0.025, -0.3 + iy * 0.025, -1.0)
                for iy in range(-4, 5) for ix in range(-4, 5)}, }
        metrics = {}
        for name, pixels in regions.items():
            rgb = [tuple(rows[y][x][channel] for x, y in sorted(pixels)) for channel in range(3)]
            means = [statistics.fmean(channel) for channel in rgb]
            metrics[name] = { LIT_SAMPLES: len(pixels), LIT_MEAN_RGB: means, "median_rgb": [statistics.median(channel) for channel in rgb], LIT_BLUE_EXCESS: means[2] - means[0], LIT_GREEN_EXCESS: means[1] - means[0], }
        # Interior grids catch missing passage/cap columns that a central mean can hide behind bright surviving pixels.
        for name, z, first_x, channel, minimum in ((LIT_VISIBLE_PASSAGE, 1.0, -1.8, 2, 15.0), (LIT_VISIBLE_CAP, 0.4, 1.05, 1, 25.0)):
            pixels = {CsgGiImageEvidence.project_point(first_x + ix * 0.1, 0.85 + iy * 0.1125, z) for iy in range(9) for ix in range(9)}
            signals = [statistics.fmean(rows[y + dy][x + dx][channel] - rows[y + dy][x + dx][0] for dy in range(-1, 2) for dx in range(-1, 2)) for x, y in sorted(pixels)]
            covered = sum(signal >= minimum for signal in signals)
            metrics[name] = { LIT_SAMPLES: len(pixels), LIT_COVERED_SAMPLES: covered, LIT_COVERAGE: covered / len(pixels), LIT_SIGNAL_THRESHOLD: minimum, LIT_MINIMUM_SIGNAL: min(signals), LIT_MEAN_SIGNAL: statistics.fmean(signals), }
        pixels = regions[LIT_MIXED_PASSAGE]
        signals = [statistics.fmean(max(rows[y + dy][x + dx][0], rows[y + dy][x + dx][2]) - rows[y + dy][x + dx][1] for dy in range(-1, 2) for dx in range(-1, 2)) for x, y in sorted(pixels)]
        covered = sum(signal >= 15.0 for signal in signals)
        metrics[LIT_VISIBLE_MIXED_PASSAGE] = { LIT_SAMPLES: len(pixels), LIT_COVERED_SAMPLES: covered, LIT_COVERAGE: covered / len(pixels), LIT_SIGNAL_THRESHOLD: 15.0, LIT_MINIMUM_SIGNAL: min(signals), LIT_MEAN_SIGNAL: statistics.fmean(signals), }
        return metrics

    @staticmethod
    def validate_metrics(metrics):
        reference, cut, uncut = (metrics[arm] for arm in ARMS)
        for arm in ARMS:
            if metrics[arm][LIT_CONTROL][LIT_BLUE_EXCESS] < 15.0:
                raise SmokeFailure(f"{arm} has no independently visible blue GI control")
        for region, signal, minimum in ((LIT_PASSAGE, LIT_BLUE_EXCESS, 15.0), (LIT_CAP_BOUNCE, LIT_GREEN_EXCESS, 8.0)):
            expected = reference[region][signal]
            if expected < minimum:
                raise SmokeFailure(f"ordinary reference has insufficient {region} GI signal: {expected:.3f}")
            if expected - uncut[region][signal] < minimum:
                raise SmokeFailure(f"uncut control does not distinguish {region} from the ordinary reference")
            # Compare converged regional color energy across different instance orders.
            tolerance = max(10.0, expected * 0.30)
            if abs(cut[region][signal] - expected) > tolerance:
                raise SmokeFailure(f"CSG {region} differs from ordinary carved geometry: " f"reference={expected:.3f}, cut={cut[region][signal]:.3f}, tolerance={tolerance:.3f}")
        if reference[LIT_CAP_SURFACE][LIT_GREEN_EXCESS] < 25.0 or cut[LIT_CAP_SURFACE][LIT_GREEN_EXCESS] < 25.0:
            raise SmokeFailure("the generated cap and ordinary reference cap are not both visibly green")
        for region in (LIT_VISIBLE_PASSAGE, LIT_VISIBLE_CAP):
            for arm in (LIT_REFERENCE, LIT_CUT):
                coverage = metrics[arm][region][LIT_COVERAGE]
                if coverage < 0.95:
                    raise SmokeFailure(f"{arm} has missing {region} geometry: coverage={coverage:.3%}, required=95%")
            coverage = uncut[region][LIT_COVERAGE]
            if coverage > 0.05:
                raise SmokeFailure(f"uncut control unexpectedly exposes {region}: coverage={coverage:.3%}, allowed=5%")
        for region in (LIT_MIXED_OPEN_SURFACE, LIT_MIXED_OPEN_SURFACE_RIGHT):
            for arm in ARMS:
                surface = metrics[arm][region][LIT_MEAN_RGB]
                if surface[0] - surface[1] < 25.0:
                    raise SmokeFailure(f"{arm} has no visible retained red {region} control")
            for channel, expected in enumerate(reference[region][LIT_MEAN_RGB]):
                actual = cut[region][LIT_MEAN_RGB][channel]
                tolerance = max(10.0, expected * 0.30)
                if abs(actual - expected) > tolerance:
                    raise SmokeFailure(f"CSG {region} brightness channel {channel} differs from ordinary geometry: " f"reference={expected:.3f}, cut={actual:.3f}, tolerance={tolerance:.3f}")
        for arm in ARMS:
            floor = metrics[arm][LIT_MIXED_FLOOR][LIT_MEAN_RGB]
            if floor[0] - floor[1] < 8.0:
                raise SmokeFailure(f"{arm} has insufficient GI bounce from retained open triangles")
        if uncut[LIT_MIXED_PASSAGE][LIT_GREEN_EXCESS] < 25.0:
            raise SmokeFailure("uncut mixed-mesh control does not expose the removed green stripe")
        for arm in (LIT_REFERENCE, LIT_CUT):
            rgb = metrics[arm][LIT_MIXED_PASSAGE][LIT_MEAN_RGB]
            if max(rgb[0], rgb[2]) - rgb[1] < 15.0:
                raise SmokeFailure(f"{arm} has insufficient GI through the clipped mixed-mesh passage")
            coverage = metrics[arm][LIT_VISIBLE_MIXED_PASSAGE][LIT_COVERAGE]
            if coverage < 0.95:
                raise SmokeFailure(f"{arm} has missing mixed-mesh passage geometry: coverage={coverage:.3%}")
        if uncut[LIT_VISIBLE_MIXED_PASSAGE][LIT_COVERAGE] > 0.05:
            raise SmokeFailure("uncut mixed-mesh control falsely exposes the GI passage")
        for region in (LIT_MIXED_PASSAGE, LIT_MIXED_FLOOR):
            for channel, expected in enumerate(reference[region][LIT_MEAN_RGB]):
                actual = cut[region][LIT_MEAN_RGB][channel]
                tolerance = max(10.0, expected * 0.30)
                if abs(actual - expected) > tolerance:
                    raise SmokeFailure(f"CSG {region} GI channel {channel} differs from ordinary geometry: " f"reference={expected:.3f}, cut={actual:.3f}, tolerance={tolerance:.3f}")
        expected_green = reference[LIT_MIXED_PASSAGE][LIT_MEAN_RGB][1]
        if cut[LIT_MIXED_PASSAGE][LIT_MEAN_RGB][1] - expected_green > max(8.0, expected_green * 0.30):
            raise SmokeFailure("the removed green open surface contributes false GI in the mixed-mesh passage")

