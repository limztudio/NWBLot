"""Offline compiler-statistics validation and exact successful-frame join; no acquisition."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import renderer_gather_benchmark as gather
from compiler_statistics_rows import CompilerStatisticsRows, LIT_UTF_8, LIT_STATUS, LIT_INPUTS, LIT_LIMITS, LIT_PATH, LIT_N, LIT_MAIN, LIT_APPEND, LIMITS
from window_capture_smoke import SmokeFailure

def file_snapshot(path):
    path = path.resolve(strict=True)
    data = path.read_bytes()
    return data, {LIT_PATH: str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--compiler-statistics", type=Path, required=True)
    parser.add_argument("--gather-result", type=Path, required=True)
    parser.add_argument("--workload", choices=gather.WORKLOADS, required=True)
    parser.add_argument("--evidence", type=Path, action=LIT_APPEND, default=[], help="Optional frozen build/source identity evidence; hashed, not inferred as provenance")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    output = args.output.resolve()
    inputs = []
    try:
        CompilerStatisticsRows.require(not output.exists(), "output must be new; existing reports are preserved")
        paths = [args.compiler_statistics, args.gather_result, Path(__file__), *args.evidence]
        for name in ("compiler_statistics_rows", "renderer_gather_benchmark", "renderer_gather_contract", "renderer_gather_comparison", "renderer_gather_measurement", "renderer_ab_benchmark", "renderer_wall_frame_evidence", "renderer_workload_log_policy", "renderer_workload_registry", "renderer_trial_comparison", "gpu_timing_evidence", "reflection_benchmark", "reflection_workload_qualification", "reflection_gpu_coverage", "window_capture_smoke", "window_capture_arguments", "window_capture_frame", "window_capture_output_reader", "window_capture_process_output", "window_capture_termination", "window_capture_process_closer", "window_capture_outcome", "smoke_volume_identity", "gpu_timing_parse", "name_symbols"):
            paths.append(Path(sys.modules[name].__file__))
        CompilerStatisticsRows.require(all(output != path.resolve() for path in paths), "output aliases an input")
        snapshots = [file_snapshot(path) for path in paths]
        inputs = [item[1] for item in snapshots]
        try:
            result = CompilerStatisticsRows.analyze_rows(CompilerStatisticsRows.decode_jsonl(snapshots[0][0]), CompilerStatisticsRows.decode_jsonl(snapshots[1][0]), args.workload)
            result[LIT_INPUTS] = inputs
            passed = True
        except (SmokeFailure, ValueError, UnicodeError, TypeError, KeyError, OverflowError) as error:
            result = {LIT_STATUS: "failed_diagnostic", "error": str(error), LIT_INPUTS: inputs, LIT_LIMITS: LIMITS}
            passed = False
        for frozen in inputs:
            CompilerStatisticsRows.require(file_snapshot(Path(frozen[LIT_PATH]))[1] == frozen, "input changed during validation: " + frozen[LIT_PATH])
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding=LIT_UTF_8, newline=LIT_N) as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
            stream.write(LIT_N)
        return 0 if passed else 1
    except (SmokeFailure, OSError, ValueError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main())
