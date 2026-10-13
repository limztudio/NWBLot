#!/usr/bin/env python3
"""Run paired SIMD benchmark workloads serially on one pinned CPU."""

import argparse
import json
import math
import statistics
import subprocess
from pathlib import Path


def run_sample(binary, cpu, operation, variant, iterations, workload, timeout):
    command = ["taskset", "-c", str(cpu), str(binary), "--measure", operation, variant, str(iterations), workload]
    completed = subprocess.run(command, check=True, capture_output=True, text=True, timeout=timeout)
    result = json.loads(completed.stdout)
    if not isinstance(result, dict) or (result.get("operation"), result.get("variant"), result.get("workload")) != (operation, variant, workload):
        raise RuntimeError(f"sample does not match requested benchmark: {operation} {variant} {workload}")
    duration = result.get("ns_per_operation")
    if isinstance(duration, bool) or not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration <= 0:
        raise RuntimeError(f"invalid benchmark timing: {duration!r}")
    if not isinstance(result.get("checksum"), str) or not result["checksum"]:
        raise RuntimeError("benchmark sample omitted its checksum")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu", type=int, default=4)
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--wide", action="store_true")
    parser.add_argument("--profiles", nargs="+")
    parser.add_argument("--binary-prefix", default="vector_benchmark")
    parser.add_argument("--operations", nargs="+")
    parser.add_argument("--iterations", type=int, default=500000)
    parser.add_argument("--timeout", type=float, default=180.0, help="Maximum seconds for each benchmark process")
    args = parser.parse_args()
    if args.cpu < 0 or args.samples <= 0 or args.iterations <= 0 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("CPU must be nonnegative; samples, iterations, and a finite timeout must be positive")
    profiles = args.profiles or (["avx2_o3"] if args.wide else ["avx2_o3", "sse2_o3", "sse4_o3"])
    cases = [(name, workload, 10000000) for name in ("dot3", "dot4", "sum") for workload in ("throughput", "latency")] if args.wide else [("dot2", "throughput", 10000000), ("dot2", "latency", 10000000), ("rotation_x", "throughput", 2000000),
        ("perspective_fov", "throughput", 2000000), ("quaternion_normal", "throughput", 2000000),]
    if args.operations:
        cases = [(name, "throughput", args.iterations) for name in args.operations]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    raw_path = args.output.with_suffix(".jsonl")
    summaries = []
    with raw_path.open("w") as raw:
        for profile in profiles:
            binary = args.root / f"{args.binary_prefix}_{profile}"
            for operation, workload, iterations in cases:
                for variant in ("baseline", "candidate"):
                    run_sample(binary, args.cpu, operation, variant, max(iterations // 20, 1), workload, args.timeout)
                samples = {"baseline": [], "candidate": []}
                for sample in range(args.samples):
                    order = ("baseline", "candidate") if sample % 2 == 0 else ("candidate", "baseline")
                    pair = {}
                    for variant in order:
                        result = run_sample(binary, args.cpu, operation, variant, iterations, workload, args.timeout)
                        result.update(profile=profile, sample=sample, cpu=args.cpu)
                        raw.write(json.dumps(result) + "\n")
                        raw.flush()
                        samples[variant].append(result["ns_per_operation"])
                        pair[variant] = result
                    if pair["baseline"]["checksum"] != pair["candidate"]["checksum"]:
                        raise RuntimeError(f"checksum mismatch: {profile} {operation} {workload}")
                baseline = statistics.median(samples["baseline"])
                candidate = statistics.median(samples["candidate"])
                summary = { "profile": profile, "operation": operation, "workload": workload, "cpu": args.cpu, "samples": args.samples, "baseline_ns": baseline, "candidate_ns": candidate, "speedup": baseline / candidate,
                    "baseline_samples": samples["baseline"], "candidate_samples": samples["candidate"], }
                summaries.append(summary)
                print(f"{profile} {operation} {workload}: {baseline:.3f} -> {candidate:.3f} ns ({baseline / candidate:.3f}x)", flush=True)
    args.output.write_text(json.dumps(summaries, indent=2) + "\n")


if __name__ == "__main__":
    main()
