"""Offline compiler-statistics validation and exact successful-frame join; no acquisition."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

import renderer_gather_benchmark as gather
from window_capture_smoke import SmokeFailure

CAPACITY = 1024
U64_MAX = (1 << 64) - 1
IDENTITY_FIELDS = ("graph_generation", "plan_generation", "device_generation", "recording_attempt_generation")
SECONDS_FIELDS = ("declaration", "analysis", "validation", "dependency_analysis", "hazard_analysis",
                  "topological_order", "queue_assignment", "planning", "total", "packetization",
                  "resource_state_planning", "packet_dependency_planning")
COUNT_FIELDS = ("task", "resource", "resource_use", "resource_version", "resource_version_edge",
                "explicit_dependency", "inferred_dependency", "external_dependency", "packet",
                "packet_dependency", "packet_external_dependency", "cross_queue_packet_dependency",
                "prologue_state_seed", "prologue_barrier", "epilogue_barrier", "state_export_barrier",
                "resource_set", "direct_resource_use", "expanded_resource_set_member_use")
SUBMISSION_FIELDS = ("accepted_packets", "accepted_tasks", "rejected_packets", "rejected_tasks")
INVALID_STATUSES = ("invalid_snapshot", "invalid_duration", "duplicate_plan", "non_monotonic_frame")
BASE_ROW_FIELDS = ("type", "sequence", "source_frame", "status")
LIMITS = ("Diagnostic attribution only. Phase timings may be nested/overlapping and must not be summed. "
          "A coherent compiler snapshot is not presentation success; the owning gather validator establishes "
          "the successful measured-frame source IDs used in this join. No gain, statistical gate, GPU-control "
          "equivalence, or optimization-retention claim is made; prior campaign results remain unchanged.")


def require(condition, message):
    if not condition:
        raise SmokeFailure(message)


def exact_fields(value, fields, label):
    require(isinstance(value, dict) and set(value) == set(fields), label + " fields differ from schema")


def integer(value, label, minimum=0):
    require(type(value) is int and minimum <= value <= U64_MAX, label + " must be an in-range integer")
    return value


def strict_object(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, "duplicate JSON key: " + key)
        value[key] = item
    return value


def invalid_constant(text):
    raise SmokeFailure("non-finite JSON constant: " + text)


def decode_jsonl(data):
    text = data.decode("utf-8")
    require(bool(text), "empty JSONL input")
    rows = []
    for number, line in enumerate(text.splitlines(), 1):
        require(bool(line.strip()), f"blank JSONL row at line {number}")
        row = json.loads(line, object_pairs_hook=strict_object, parse_constant=invalid_constant)
        require(isinstance(row, dict), f"JSONL row {number} must be an object")
        rows.append(row)
    return rows


def validate_compiler_rows(rows):
    require(len(rows) >= 2, "compiler configuration and complete footer are required")
    configuration, footer = rows[0], rows[-1]
    exact_fields(configuration, ("type", "schema", "capacity"), "compiler configuration")
    require(configuration["type"] == "compiler_statistics_configuration", "compiler configuration must be first")
    require(type(configuration["schema"]) is int and configuration["schema"] == 1,
            "unsupported compiler schema")
    require(type(configuration["capacity"]) is int and configuration["capacity"] == CAPACITY,
            "compiler capture capacity changed")
    exact_fields(footer, ("type", "attempts", "stored", "overflow", "invalid", "complete"), "compiler footer")
    require(footer["type"] == "compiler_statistics_complete", "compiler complete footer must be last")
    for key in ("attempts", "stored", "overflow", "invalid"):
        integer(footer[key], "footer " + key)
    require(type(footer["complete"]) is bool, "footer complete flag must be boolean")
    captures = rows[1:-1]
    require(len(captures) <= CAPACITY, "compiler capture exceeds fixed capacity")
    require(footer["stored"] == len(captures) == min(footer["attempts"], CAPACITY),
            "footer attempt/stored counts differ from raw rows")
    require(footer["attempts"] == footer["stored"] + footer["overflow"], "footer overflow accounting differs")
    previous_frame, plans, invalid = None, set(), []
    for index, row in enumerate(captures):
        require(row.get("type") == "compiler_statistics", "unexpected compiler row type")
        status = row.get("status")
        require(status == "valid" or status in INVALID_STATUSES, "unknown compiler status")
        fields = (*BASE_ROW_FIELDS, "identity", "seconds", "counts", "submission") if status == "valid" else BASE_ROW_FIELDS
        exact_fields(row, fields, "compiler row")
        require(integer(row["sequence"], "raw sequence") == index, "raw compiler sequence is missing, duplicated or reordered")
        frame = integer(row["source_frame"], "source frame")
        require(previous_frame is None or frame > previous_frame, "raw compiler source-frame order is not strictly increasing")
        previous_frame = frame
        if status != "valid":
            invalid.append({"sequence": index, "source_frame": frame, "status": status})
            continue
        exact_fields(row["identity"], IDENTITY_FIELDS, "compiler identity")
        for key, value in row["identity"].items():
            integer(value, "identity " + key, 1)
        require(row["identity"]["device_generation"] <= 65535, "identity device_generation exceeds native u16")
        plan = tuple(row["identity"][key] for key in ("device_generation", "graph_generation", "plan_generation"))
        require(plan not in plans, "duplicate compiler plan identity, regardless of recording attempt")
        plans.add(plan)
        exact_fields(row["seconds"], SECONDS_FIELDS, "compiler seconds")
        for key, value in row["seconds"].items():
            require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
                    "invalid finite nonnegative duration: " + key)
        exact_fields(row["counts"], COUNT_FIELDS, "compiler counts")
        exact_fields(row["submission"], SUBMISSION_FIELDS, "compiler submission")
        for key, value in (*row["counts"].items(), *row["submission"].items()):
            integer(value, "count " + key)
    require(footer["invalid"] == len(invalid), "footer invalid count differs from raw status flags")
    complete = bool(captures) and footer["overflow"] == 0
    require(footer["complete"] == complete, "footer complete flag contradicts raw capture")
    require(complete, "compiler capture is incomplete: " + json.dumps({"overflow": footer["overflow"], "invalid": invalid}))
    require(not any(row["status"] in ("duplicate_plan", "non_monotonic_frame") for row in invalid),
            "structural compiler identity/order status is invalid")
    return captures


def analyze_rows(compiler_rows, gather_rows, workload):
    # This is the owning production parser, including its CPU/GPU/settings/complete-result gates.
    validated_gather = gather.summarize_rows(gather_rows, workload, "timing")
    captures = validate_compiler_rows(compiler_rows)
    measured = [row for row in gather_rows if row.get("type") == "cpu"]
    require(len(measured) == 256, "exactly 256 successful measured frame rows are required")
    by_source = {row["source_frame"]: row for row in captures}
    joined = []
    for frame in measured:
        source = frame["source_frame"]
        require(source in by_source, "missing compiler snapshot for successful measured source frame " + str(source))
        compiler = by_source[source]
        require(compiler["status"] == "valid", "invalid compiler snapshot for successful measured source frame " + str(source))
        joined.append({"source_frame": source, "gather_publish_frame": frame["publish_frame"],
                       "compiler_sequence": compiler["sequence"], "identity": compiler["identity"],
                       "seconds": compiler["seconds"], "counts": compiler["counts"], "submission": compiler["submission"]})
    summaries = {}
    for phase in SECONDS_FIELDS:
        values = [row["seconds"][phase] for row in joined]
        summaries[phase] = {"samples": len(values), "mean_seconds": math.fsum(values) / len(values),
                            "minimum_seconds": min(values), "maximum_seconds": max(values)}
    measured_ids = {row["source_frame"] for row in measured}
    invalid = [row for row in captures if row["status"] != "valid"]
    return {"status": "clean_joined_diagnostic" if not invalid else "joined_diagnostic_with_invalid_unmeasured",
            "clean": not invalid, "raw_invalid_count": len(invalid), "invalid_unmeasured_rows": invalid,
            "workload": workload,
            "raw_attempts": compiler_rows[-1]["attempts"], "raw_stored": len(captures),
            "joined_frames": len(joined), "source_window": validated_gather["source_window"],
            "unjoined_source_frames": [row["source_frame"] for row in captures if row["source_frame"] not in measured_ids],
            "phase_seconds": summaries, "joined": joined, "validated_gather": validated_gather,
            "raw_compiler_rows": compiler_rows, "limits": LIMITS}


def file_snapshot(path):
    path = path.resolve(strict=True)
    data = path.read_bytes()
    return data, {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--compiler-statistics", type=Path, required=True)
    parser.add_argument("--gather-result", type=Path, required=True)
    parser.add_argument("--workload", choices=gather.WORKLOADS, required=True)
    parser.add_argument("--evidence", type=Path, action="append", default=[],
                        help="Optional frozen build/source identity evidence; hashed, not inferred as provenance")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    output = args.output.resolve()
    inputs = []
    try:
        require(not output.exists(), "output must be new; existing reports are preserved")
        paths = [args.compiler_statistics, args.gather_result, Path(__file__), *args.evidence]
        for name in ("renderer_gather_benchmark", "renderer_ab_benchmark", "reflection_benchmark",
                     "window_capture_smoke", "smoke_volume_identity", "gpu_timing_parse", "name_symbols"):
            paths.append(Path(sys.modules[name].__file__))
        require(all(output != path.resolve() for path in paths), "output aliases an input")
        snapshots = [file_snapshot(path) for path in paths]
        inputs = [item[1] for item in snapshots]
        try:
            result = analyze_rows(decode_jsonl(snapshots[0][0]), decode_jsonl(snapshots[1][0]), args.workload)
            result["inputs"] = inputs
            passed = True
        except (SmokeFailure, ValueError, UnicodeError, TypeError, KeyError, OverflowError) as error:
            result = {"status": "failed_diagnostic", "error": str(error), "inputs": inputs, "limits": LIMITS}
            passed = False
        for frozen in inputs:
            require(file_snapshot(Path(frozen["path"]))[1] == frozen, "input changed during validation: " + frozen["path"])
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
            stream.write("\n")
        return 0 if passed else 1
    except (SmokeFailure, OSError, ValueError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
