"""Evaluate paired refraction density timing results."""

import statistics

from renderer_workload_registry import AVBOIT as RENDERER_AVBOIT
from experiment_statistics import PairedExperimentAnalysis
from window_capture_smoke import SmokeFailure


FRAME = "render.frame"
RESOLVE = "render.avboit.refraction_resolve"
CLASSIFY = "render.avboit.refraction_classify_tiles"
CLEAR_ARGUMENTS = "render.avboit.refraction_tile_arguments_clear"
COMPOSITE = "render.deferred_composite"
CONTROLS = ("render.opaque_regular", "render.shadow_visibility", "render.deferred_lighting", "render.deferred_present")
OBSERVATIONS = (COMPOSITE,)
AVBOIT = tuple(RENDERER_AVBOIT)
OBSERVED = (FRAME, RESOLVE, CLASSIFY, CLEAR_ARGUMENTS, *CONTROLS, *OBSERVATIONS, *AVBOIT)
EFFECT_COST = (CLEAR_ARGUMENTS, CLASSIFY, RESOLVE, COMPOSITE)


class RefractionDensityComparison:
    @staticmethod
    def reference_contract(baseline_scheduling):
        if baseline_scheduling == "full-frame":
            return "Surgical full-frame renderer reference with identical AVBOIT/compositor/fixture; frozen source manifest records the exact patch. Candidate uses tile scheduling."
        if baseline_scheduling == "tiled":
            return "Frozen tiled renderer reference versus tiled candidate with identical fixture instrumentation; both arms must publish argument clear and classification. Source manifests record the scheduling difference."
        raise SmokeFailure("invalid baseline scheduling strategy: " + str(baseline_scheduling))

    @staticmethod
    def combined_work_policy(baseline_scheduling):
        admission = "baseline omits tile-only clear/classification" if baseline_scheduling == "full-frame" else "both arms require tile clear/classification"
        return "Argument clear + classification + resolve + compositor; " + admission + "; whole frame includes all work."

    @staticmethod
    def comparison(trials, blocks, baseline_scheduling="full-frame"):
        contract = RefractionDensityComparison.reference_contract(baseline_scheduling)
        expected = {(block, arm) for block in range(blocks) for arm in ("baseline", "candidate")}
        observed = [(trial["block"], trial["arm"]) for trial in trials]
        if len(observed) != len(set(observed)) or set(observed) != expected:
            raise SmokeFailure("density comparison refuses duplicate or incomplete paired blocks")
        for trial in trials:
            strategy = baseline_scheduling if trial["arm"] == "baseline" else "tiled"
            if trial.get("scheduling", strategy) != strategy or trial.get("baseline_scheduling", baseline_scheduling) != baseline_scheduling:
                raise SmokeFailure("density comparison refuses mixed or mismatched scheduling strategies")
            tile_scopes = (CLASSIFY, CLEAR_ARGUMENTS)
            if strategy == "tiled" and any(scope not in trial["scopes"] for scope in tile_scopes):
                raise SmokeFailure("tiled density comparison lacks required clear/classification scopes")
            if strategy == "full-frame" and any(scope in trial["scopes"] for scope in tile_scopes):
                raise SmokeFailure("full-frame density comparison contains forbidden tile clear/classification scopes")
        paired = {index: {trial["arm"]: trial for trial in trials if trial["block"] == index} for index in range(blocks)}
        metrics = {"frame_gpu_ms": lambda trial: trial["scopes"][FRAME]["mean_ms"], "refraction_classification_plus_resolve_per_frame_ms": lambda trial: trial["refraction_work_per_frame_ms"],
            "argument_clear_plus_classification_plus_resolve_plus_compositor_per_frame_ms": lambda trial: sum(trial["scopes"].get(scope, {}).get("total_ms", 0) for scope in EFFECT_COST) / trial["scopes"][FRAME]["gpu_samples"]}
        metrics.update({scope: lambda trial, scope=scope: trial["scopes"][scope]["mean_ms"] for scope in (*CONTROLS, *OBSERVATIONS, *AVBOIT, RESOLVE)})
        if all(trial["wall_frame"]["eligible"] for trial in trials):
            metrics["wall_callback_frame_ms"] = lambda trial: trial["wall_frame"]["mean_frame_ms"]
        result = {}
        for name, getter in metrics.items():
            baseline = [getter(paired[index]["baseline"]) for index in range(blocks)]
            candidate = [getter(paired[index]["candidate"]) for index in range(blocks)]
            deltas = [after - before for before, after in zip(baseline, candidate)]
            before, after = statistics.fmean(baseline), statistics.fmean(candidate)
            result[name] = {"baseline": before, "candidate": after, "candidate_minus_baseline": statistics.fmean(deltas),
                "percent_change": 100 * (after - before) / before if before else None, "block_differences": deltas}
            if blocks >= 5:
                result[name]["paired_statistics"] = PairedExperimentAnalysis().paired_statistics(deltas)
        wall_rejected = [{"block": trial["block"], "arm": trial["arm"], "reason": trial["wall_frame"].get("reason")} for trial in trials if not trial["wall_frame"]["eligible"]]
        return {"status": f"descriptive_{blocks}_blocks_no_confidence_claim" if blocks < 5 else "paired_intervals_require_control_assessment", "metrics": result,
            "baseline_scheduling": baseline_scheduling, "candidate_scheduling": "tiled", "reference_contract": contract,
            "controls": list(CONTROLS), "affected_observed_scopes": list(OBSERVATIONS),
            "wall_rejected_trials": wall_rejected, "control_assessment": "Report control movement explicitly; host measurement floor is not established by this acquisition.",
            "combined_work_policy": RefractionDensityComparison.combined_work_policy(baseline_scheduling),
            "interpretation": "Combined work is normalized by completed GPU frames. Compositor is affected, not a control. Wall callback time includes pacing; it is neither isolated CPU cost nor measured presentation cadence. No FPS claim follows from these counters."}

