"""Validate optical and caustic contributions in captured framebuffers."""
import math

from caustic_optical_reference import exterior_samples, expected_environment_color, predictor_metadata
from reflection_image_analysis import ReflectionImageAnalysis
from window_capture_smoke import SmokeFailure

LIT_COMBINED = "combined"
LIT_REFLECTION_DISABLED = "reflection_disabled"
LIT_CAUSTICS_DISABLED = "caustics_disabled"
LIT_REFRACTION_DISABLED = "refraction_disabled"
LIT_EXTERIOR_ENVIRONMENT = "exterior_environment"
VARIANTS = (LIT_COMBINED, LIT_REFLECTION_DISABLED, LIT_CAUSTICS_DISABLED, LIT_REFRACTION_DISABLED)


class CausticOpticalImageEvidence:
    @staticmethod
    def analyze_exterior_reflection(combined, disabled):
        width, height, rows = ReflectionImageAnalysis.validate_frame(combined)
        if disabled[:2] != combined[:2]:
            raise SmokeFailure("exterior reflection captures have different dimensions")
        selected = exterior_samples(width, height)
        tested, weak, errors, ratios, missing = 0, 0, [], [], []
        for x, y, fresnel in selected:
            base, observed = disabled[2][y][x], rows[y][x]
            expected = expected_environment_color(base, fresnel)
            expected_gain = sum(a - b for a, b in zip(expected, base)) / 3
            if expected_gain < 8:
                weak += 1
                continue
            observed_gain = sum(a - b for a, b in zip(observed, base)) / 3
            tested += 1
            ratios.append(observed_gain / expected_gain)
            errors.extend(abs(a - b) for a, b in zip(observed, expected))
            # Allow two display bytes for quantization and require at least half of the expected signal.
            if observed_gain + 2 < 0.5 * expected_gain:
                missing.append((x, y))
        if tested < 2000 or weak > len(selected) * 0.1:
            raise SmokeFailure("too few mesh-proven exterior samples with visible environment reflection")
        if missing:
            raise SmokeFailure("missing exterior Fresnel/environment reflection at " + str(len(missing)) + " stable mesh samples; first pixels " + str(missing[:8]))
        errors.sort()
        mae = sum(errors) / len(errors)
        if mae > 1.5:
            raise SmokeFailure("exterior reflection disagrees with the fixed-environment radiometric oracle: byte MAE " + str(mae))
        return {"predictor": predictor_metadata(), "mesh_selected_samples": len(selected), "tested_samples": tested, "weak_samples": weak, "missing_samples": 0, "minimum_predicted_gain_fraction": min(ratios),
            "rgb_byte_mae": mae, "rgb_byte_p99_error": errors[min(len(errors) - 1, int(len(errors) * 0.99))]}

    @staticmethod
    def analyze_frames(frames, software_ray_tracing=False):
        combined = frames[LIT_COMBINED]
        width, height, rows = ReflectionImageAnalysis.validate_frame(combined)
        if any(frame[:2] != combined[:2] for frame in frames.values()):
            raise SmokeFailure("combined optical captures have different dimensions")
        # Projected sphere bounds for camera at z=-2.2 and radius 0.7.
        radius = height / (2 * math.tan(math.pi / 6)) * 0.7 / math.sqrt(2.2 ** 2 - 0.7 ** 2)
        results = {}
        for variant in VARIANTS[1:]:
            sphere_changed, ground_changed, sphere_absolute, outside_absolute, outside_channels, ground_gain = 0, 0, 0, 0, 0, 0
            ground_cells = {}
            for y in range(height):
                for x in range(width):
                    a, b = rows[y][x], frames[variant][2][y][x]
                    delta = sum(abs(i - j) for i, j in zip(a, b))
                    in_sphere = (x + 0.5 - width / 2) ** 2 + (y + 0.5 - height / 2) ** 2 <= (radius + 3) ** 2
                    if in_sphere:
                        sphere_changed += delta > 9
                        sphere_absolute += delta
                    else:
                        outside_absolute += delta
                        outside_channels += 3
                    if not in_sphere and y > height * 0.78 and width * 0.15 < x < width * 0.85:
                        ground_changed += delta > 9
                        gain = sum(a) - sum(b)
                        ground_gain += gain
                        if gain > 0:
                            # Focused caustic gain must concentrate in a few 8x8 cells rather than spread across the ground.
                            key = (x // (width // 8 + 1), y // (height // 8 + 1))
                            ground_cells[key] = ground_cells.get(key, 0) + gain
            if variant == LIT_REFLECTION_DISABLED:
                if sphere_changed < max(100, math.pi * radius * radius * 0.005):
                    raise SmokeFailure("reflection toggle produced no visible reflected contribution on the glass sphere")
                if outside_absolute / max(outside_channels, 1) > 0.6:
                    raise SmokeFailure("reflection toggle changed the nonreflective ground/caustic control")
            elif variant == LIT_CAUSTICS_DISABLED:
                positive_gain = sum(ground_cells.values())
                ordered_cells = sorted(ground_cells.values(), reverse=True)
                focus_share = sum(ordered_cells[:3]) / positive_gain if positive_gain > 0 else 0.0
                # Measure concentrated positive gain because bright ground tonemapping compresses per-pixel deltas.
                if ground_gain < 1500 or focus_share < 0.4:
                    raise SmokeFailure("caustic toggle produced no visible positive photon contribution on the ground")
            elif sphere_changed < max(100, math.pi * radius * radius * 0.005):
                raise SmokeFailure("camera refraction toggle produced no visible sphere transmission change")
            results[variant] = {"changed_sphere_pixels": sphere_changed, "sphere_absolute_channel_delta": sphere_absolute, "changed_ground_pixels": ground_changed, "ground_channel_gain": ground_gain,
                "outside_sphere_byte_mae": outside_absolute / max(outside_channels, 1)}
        if software_ray_tracing:
            results[LIT_REFLECTION_DISABLED][LIT_EXTERIOR_ENVIRONMENT] = { "evaluated": False, "reason": "Hardware-only geometric ray-miss oracle; screen-space hits are projected approximations.", }
        else:
            results[LIT_REFLECTION_DISABLED][LIT_EXTERIOR_ENVIRONMENT] = CausticOpticalImageEvidence.analyze_exterior_reflection(combined, frames[LIT_REFLECTION_DISABLED])
        return results

