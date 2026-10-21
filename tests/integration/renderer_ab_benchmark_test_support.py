"""Shared trial, manifest, and log builders for the renderer A/B benchmark analysis test modules."""


from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark


LIT_A = "a"
LIT_B = "b"
LIT_TOTAL_MS = "total_ms"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_MEAN_MS = "mean_ms"
LIT_TRANSPARENT_MULTI = "transparent-multi"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_ARM = "arm"
LIT_BLOCK = "block"
LIT_POSITION = "position"
LIT_SCOPES = "scopes"


def scopes(workload, frames=200, frame_ms=5.0):
    return {name: {LIT_TOTAL_MS: (frame_ms if name == benchmark.FRAME else .5) * frames * multiplier, LIT_GPU_SAMPLES: frames * multiplier, LIT_REPORTS: 6, LIT_MEAN_MS: frame_ms if name == benchmark.FRAME else .5}
        for name, multiplier in workload.scope_multipliers}


def trial_matrix(frame_delta=-.5):
    workload = benchmark.workloads()[LIT_TRANSPARENT_MULTI]
    arms = (benchmark.Arm(LIT_BASELINE, Path(LIT_A), Path("a_runtime"), Path("a.json")), benchmark.Arm(LIT_CANDIDATE, Path(LIT_B), Path("b_runtime"), Path("b.json")))
    orders = [[arm.name for arm in row] for row in benchmark.balanced_orders(arms, 8, 0)]
    trials = []
    for block, row in enumerate(orders):
        for position, arm in enumerate(row):
            current = scopes(workload, frame_ms=5 + block * .01 + (frame_delta if arm == LIT_CANDIDATE else 0))
            trials.append({LIT_ARM: arm, LIT_BLOCK: block, LIT_POSITION: position, benchmark.LIT_POSITIVE_FRAME_REPORTS: 11, LIT_SCOPES: current})
    return workload, orders, trials
