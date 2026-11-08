"""Validate long-miss qualification against the timed workload."""
import json
import math

from smoke_volume_identity import authored_volume_hashes, file_identity
from window_capture_smoke import SmokeFailure

LIT_ACTUAL_ATTEMPTS = "actual_attempts"
LIT_ACTUAL_ITERATIONS = "actual_iterations"
LIT_ACTUAL_STEP_LIMIT_MISSES = "actual_step_limit_misses"
LIT_BUILD_IDENTITY = "build_identity"
LIT_CASE = "case"
LIT_COMPLETED_OBSERVATIONS = "completed_observations"
LIT_FEEDBACK_LONG_MISS = "feedback_long_miss"
LIT_FIXED_DELTA_SECONDS = "fixed_delta_seconds"
LIT_HYBRID = "hybrid"
LIT_METRICS = "metrics"
LIT_NAME = "name"
LIT_OPTICAL_QUERIES = "optical_queries"
LIT_PATH = "path"
LIT_ROUGHNESS = "roughness"
LIT_SCOPE = "scope"
LIT_SCREEN_STEPS = "screen_steps"
LIT_UTF_8 = "utf-8"


class ReflectionWorkloadQualification:
    @staticmethod
    def validate_long_miss_qualification(args, executable_identity):
        """Accept the semantic runner's evidence only for this exact build and stationary workload."""
        if args.family != LIT_FEEDBACK_LONG_MISS:
            return None
        if not args.qualification:
            raise SmokeFailure("feedback_long_miss requires a matching diagnostics-on --qualification manifest")
        try:
            evidence = json.loads(args.qualification.read_text(encoding=LIT_UTF_8))
        except (ValueError, OSError) as error:
            raise SmokeFailure(f"cannot read costly-miss qualification: {error}") from error
        if not isinstance(evidence, dict):
            raise SmokeFailure("costly-miss qualification must be a report object")
        build = evidence.get(LIT_BUILD_IDENTITY, {})
        volumes = authored_volume_hashes(args.working_directory)
        if not isinstance(build, dict) or build.get("executable_sha256") != executable_identity["sha256"] \
                or not volumes or build.get("asset_volumes") != volumes:
            raise SmokeFailure("costly-miss qualification does not match the executable and authored packed volumes")
        if evidence.get("temporal") is not False or evidence.get("spatial_filter") is not False \
                or evidence.get(LIT_OPTICAL_QUERIES) != args.optical_queries \
                or not isinstance(evidence.get(LIT_FIXED_DELTA_SECONDS), (int, float)) \
                or not math.isclose(evidence[LIT_FIXED_DELTA_SECONDS], 1 / 60, abs_tol=1e-9):
            raise SmokeFailure("costly-miss qualification has incompatible stationary estimator settings")
        captures = evidence.get("captures", [])
        if not isinstance(captures, list) or any(not isinstance(capture, dict) for capture in captures):
            raise SmokeFailure("costly-miss qualification captures must be explicit report objects")
        for name, enabled in (("long_miss_baseline", False), ("long_miss_feedback", True)):
            matched = [capture for capture in captures if capture.get(LIT_NAME) == name]
            if len(matched) != 1:
                raise SmokeFailure("costly-miss qualification needs exactly one matching off/on capture pair")
            capture = matched[0]
            expected = {LIT_CASE: LIT_FEEDBACK_LONG_MISS, "mode": LIT_HYBRID, "enabled": enabled, "hardware_budget": args.ray_budget, LIT_SCREEN_STEPS: args.screen_steps, "seed": args.sampling_seed,
                LIT_ROUGHNESS: 0.0, "final_state": False, "mutation": False, "extent": "native"}
            if any(capture.get(key) != value for key, value in expected.items()) or (args.width, args.height) != (960, 720):
                raise SmokeFailure("costly-miss qualification scene, extent, seed or tracing settings differ from timing")
        metrics = evidence.get(LIT_METRICS, {})
        if not isinstance(metrics, dict) or not metrics.get("long_miss_geometry") or not metrics.get("long_miss"):
            raise SmokeFailure("costly-miss qualification lacks independent geometry and paired image evidence")
        qualified = metrics.get("costly_miss_qualification", {})
        integer_fields = (LIT_COMPLETED_OBSERVATIONS, LIT_ACTUAL_ATTEMPTS, LIT_ACTUAL_ITERATIONS, LIT_ACTUAL_STEP_LIMIT_MISSES)
        if not isinstance(qualified, dict) or any(type(qualified.get(key)) is not int or qualified[key] < 0 for key in integer_fields):
            raise SmokeFailure("costly-miss qualification lacks exact completed work totals")
        attempts, iterations = qualified[LIT_ACTUAL_ATTEMPTS], qualified[LIT_ACTUAL_ITERATIONS]
        limited = qualified[LIT_ACTUAL_STEP_LIMIT_MISSES]
        if qualified[LIT_COMPLETED_OBSERVATIONS] < 8 or attempts <= 0 or limited > attempts \
                or not limited * args.screen_steps <= iterations <= attempts * args.screen_steps \
                or iterations / attempts < 12.0 or limited / attempts < .2:
            raise SmokeFailure("fixture has not established costly bounded SSR misses")
        for key, actual in (("mean_iterations_per_attempt", iterations / attempts), ("step_limit_miss_fraction", limited / attempts)):
            value = qualified.get(key)
            if not isinstance(value, (float, int)) or not math.isfinite(value) or not math.isclose(value, actual, rel_tol=1e-9):
                raise SmokeFailure("costly-miss qualification aggregate ratios contradict its work totals")
        return {LIT_PATH: str(args.qualification), **file_identity(args.qualification), LIT_METRICS: qualified, LIT_BUILD_IDENTITY: build, "settings_match": True, LIT_SCOPE: "separate diagnostics-on workload qualification; no GPU timing or speed claim"}

