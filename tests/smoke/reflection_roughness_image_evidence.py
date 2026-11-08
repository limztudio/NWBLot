"""Compare reflection roughness and related image evidence."""

from reflection_image_analysis import ReflectionImageAnalysis
from reflection_roughness_reference import frame_grid
from window_capture_smoke import SmokeFailure


LIT_MATCHED_SCENE_CAPTURES_HAVE_DIFFERENT_ = "matched scene captures have different dimensions"
LIT_MEAN_CHANNEL_BYTE_ERROR = "mean_channel_byte_error"
LIT_CHANNELS_DIFFERING_BY_MORE_THAN_TWO = "channels_differing_by_more_than_two"


class ReflectionRoughnessImageEvidence:
    @staticmethod
    def compare_exact_scene(left, right, maximum_mean_error=0.15):
        if left[:2] != right[:2]:
            raise SmokeFailure(LIT_MATCHED_SCENE_CAPTURES_HAVE_DIFFERENT_)
        width, height, rows = ReflectionImageAnalysis.validate_frame(left)
        differences = [abs(a - b) for y in range(height) for x in range(width) for a, b in zip(rows[y][x], right[2][y][x])]
        mean = sum(differences) / len(differences)
        changed = sum(value > 2 for value in differences)
        if mean > maximum_mean_error or changed > width * height * 3 * 0.005:
            raise SmokeFailure(f"matched final scenes retain a stale or altered reflection ({mean:.4f} byte MAE; {changed} channels)")
        return {LIT_MEAN_CHANNEL_BYTE_ERROR: mean, LIT_CHANNELS_DIFFERING_BY_MORE_THAN_TWO: changed}

    @staticmethod
    def compare_smooth_glass_scene(left, right, maximum_mean_error=0.30):
        # Roughness only affects non-glass receivers, so both glass captures share the same stripe geometry.
        if left[:2] != right[:2]:
            raise SmokeFailure(LIT_MATCHED_SCENE_CAPTURES_HAVE_DIFFERENT_)
        width, height, rows = ReflectionImageAnalysis.validate_frame(left)
        differences = [abs(a - b) for y in range(height) for x in range(width) for a, b in zip(rows[y][x], right[2][y][x])]
        mean = sum(differences) / len(differences)
        changed = sum(value > 2 for value in differences)
        changed_pixels = sum(1 for y in range(height) for x in range(width) if max(abs(a - b) for a, b in zip(rows[y][x], right[2][y][x])) > 2)
        if mean > maximum_mean_error or changed_pixels > width * height * 0.05:
            raise SmokeFailure(f"matched smooth-glass scenes diverged ({mean:.4f} byte MAE; {changed_pixels} pixels)")
        return {LIT_MEAN_CHANNEL_BYTE_ERROR: mean, LIT_CHANNELS_DIFFERING_BY_MORE_THAN_TWO: changed, "pixels_differing_by_more_than_two": changed_pixels}

    @staticmethod
    def high_frequency_energy(frame):
        cells, _, _, _ = frame_grid(frame)
        left, top = cells[0][:2]
        right, bottom = cells[-1][2:]
        from reflection_roughness_reference import DECODED_RADIANCE
        rows = frame[2]
        total = 0.0
        for y in range(top + 1, bottom - 1):
            for x in range(left + 1, right - 1):
                for channel in (0, 1):
                    center = DECODED_RADIANCE[rows[y][x][channel]]
                    neighbors = sum(DECODED_RADIANCE[rows[yy][xx][channel]] for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))) * 0.25
                    total += (center - neighbors) ** 2
        return total

    @staticmethod
    def compare_deformation(bind, deformed):
        if bind[:2] != deformed[:2]:
            raise SmokeFailure("deformation captures have different dimensions")
        def mask(frame, channel):
            return {(x, y) for y, row in enumerate(frame[2]) for x, pixel in enumerate(row) if pixel[channel] >= 40 and all(pixel[channel] >= pixel[other] + 25 for other in range(3) if other != channel)}
        before, after = mask(bind, 0), mask(deformed, 0)
        if min(len(before), len(after)) < 30:
            raise SmokeFailure("deformation comparison is missing a visible reflected red model")
        changed = len(before ^ after)
        if changed < max(12, len(before) * 0.1):
            raise SmokeFailure("skeletal mutation did not change the reflected model footprint")
        green_before, green_after = mask(bind, 1), mask(deformed, 1)
        if min(len(green_before), len(green_after)) < 100 or len(green_before ^ green_after) > max(4, len(green_before) * 0.01):
            raise SmokeFailure("skeletal mutation lost or changed the unrelated green reflection")
        return {"bind_red_pixels": len(before), "posed_red_pixels": len(after), "changed_red_footprint_pixels": changed, "retained_green_pixels": len(green_before & green_after)}

    @staticmethod
    def compare_sampling_seeds(first, second):
        if first[:2] != second[:2]:
            raise SmokeFailure("sampling-seed captures have different dimensions")
        from reflection_roughness_oracle import ReflectionRoughnessOracle
        changed, total, absolute_error = 0, 0, 0
        for left, top, right, bottom in ReflectionRoughnessOracle.mirror_cells(*first[:2]):
            for y in range(top, bottom):
                for x in range(left, right):
                    differences = [abs(a - b) for a, b in zip(first[2][y][x], second[2][y][x])]
                    changed += int(max(differences) > 2)
                    absolute_error += sum(differences)
                    total += 1
        if changed < max(64, total * 0.005):
            raise SmokeFailure("independent sampling seeds did not change unresolved rough-reflection samples")
        return {"changed_receiver_pixels": changed, "receiver_pixels": total, "mean_receiver_channel_byte_difference": absolute_error / (total * 3)}

    @staticmethod
    def compare_convergence(error8, error64, error256):
        if not (0 <= error256 < error64 < error8) or error256 >= error8 * 0.8:
            raise SmokeFailure("8/64/256 accepted samples did not converge monotonically toward the independent GGX reference")
        return {"accepted_caps": [8, 64, 256], "normalized_reference_errors": [error8, error64, error256]}

