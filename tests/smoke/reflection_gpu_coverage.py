"""Validate completed GPU timing coverage for reflection workloads."""
import math

from window_capture_smoke import SmokeFailure

LIT_SCREEN = "screen"
LIT_HYBRID = "hybrid"
LIT_HARDWARE = "hardware"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_MEAN_MS = "mean_ms"
FRAME = "render.frame"
CLASSIFY = "render.reflection_classify"
DEPTH = "render.reflection_depth_pyramid"
BUILD_ARGS = "render.reflection_build_args"
HARDWARE = "render.reflection_hardware"
TEMPORAL = "render.reflection_temporal"
SPATIAL = "render.reflection_spatial"
CONTROLS = ("render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting")
OBSERVED_CONTROLS = CONTROLS + ("render.deferred_composite", "render.deferred_present")
KERNELS = (CLASSIFY, DEPTH, BUILD_ARGS, HARDWARE, TEMPORAL, SPATIAL)
KNOWN_SCOPES = (FRAME,) + OBSERVED_CONTROLS + KERNELS


class ReflectionGpuCoverage:
    @staticmethod
    def required_scopes(variant, history_samples=16):
        required = [FRAME, *OBSERVED_CONTROLS, CLASSIFY]
        if variant.mode in (LIT_SCREEN, LIT_HYBRID):
            required.append(DEPTH)
        if variant.mode in (LIT_HARDWARE, LIT_HYBRID):
            required.extend((BUILD_ARGS, HARDWARE))
        if variant.temporal and history_samples > 1:
            required.append(TEMPORAL)
        if variant.spatial:
            required.append(SPATIAL)
        return required

    @staticmethod
    def validate_coverage(summaries, variant, report_count, minimum_frames, mip_count, history_samples=16):
        if variant.temporal and history_samples <= 1 and TEMPORAL in summaries:
            raise SmokeFailure("single-sample temporal history recorded an unexpected native dispatch")
        missing = [scope for scope in ReflectionGpuCoverage.required_scopes(variant, history_samples) if scope not in summaries]
        if missing:
            raise SmokeFailure("missing GPU scopes (including possible probe scope-capacity loss): " + ", ".join(missing))
        frame_count = summaries[FRAME][LIT_GPU_SAMPLES]
        if frame_count < minimum_frames:
            raise SmokeFailure(f"only {frame_count} completed GPU frames; {minimum_frames} required")
        for scope in ReflectionGpuCoverage.required_scopes(variant, history_samples):
            entry = summaries[scope]
            if entry[LIT_GPU_SAMPLES] < minimum_frames // 2:
                raise SmokeFailure(f"scope {scope} has too few completed GPU samples")
            if entry[LIT_REPORTS] < math.ceil(report_count * 0.75):
                raise SmokeFailure(f"scope {scope} is absent in too many timing reports")
            if scope in KERNELS:
                multiplier = mip_count if scope == DEPTH else 1
                expected = frame_count * multiplier
                if abs(entry[LIT_GPU_SAMPLES] - expected) > max(2 * multiplier, expected * 0.02):
                    raise SmokeFailure(f"scope {scope} sample ratio does not match {multiplier} dispatch(es) per GPU frame")
        if summaries[FRAME][LIT_MEAN_MS] <= 0:
            raise SmokeFailure("GPU frame timing is zero")

    @staticmethod
    def kernel_work_ms(summaries, variant, mip_count, history_samples=16):
        return sum(summaries[scope][LIT_MEAN_MS] * (mip_count if scope == DEPTH else 1) for scope in ReflectionGpuCoverage.required_scopes(variant, history_samples) if scope in KERNELS)

