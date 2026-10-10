#!/usr/bin/env python3
"""Live small/dense/ordinary CSG reflection transitions with exact accepted source-frame evidence."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys

from reflection_optical_smoke import OpticalCapture, analyze_image, compare_invariant, parse_optics, validate_optics
from reflection_roughness_smoke import CaptureSpec, parse_history, validate_history
from reflection_smoke import DEFAULT_RAY_BUDGET, capture_environment, parse_statistics
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure, read_bmp_24_rows

PHASES = (("small_first", "optical_csg_cap", 12, 1), ("dense_first", "optical_csg_dense", 108, 1),
    ("ordinary_dense", "optical_dense", 0, 0), ("small_return", "optical_csg_cap", 12, 1),
    ("dense_return", "optical_csg_dense", 108, 1))
CASE = "optical_csg_context_transition"
PHASE_FIELDS = ("phase", "source_frame", "cut_receiver_primitives", "csg")
CAPTURE_FIELDS = ("phase", "source_frame", "completed_frames")


def records(log_text, marker, fields):
    result = []
    for line in log_text.splitlines():
        if marker not in line:
            continue
        record = {}
        for field in line.split(marker, 1)[1].strip().split():
            match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
            if not match or match[1] in record:
                raise SmokeFailure("malformed context transition source metadata")
            record[match[1]] = int(match[2])
        if set(record) != set(fields):
            raise SmokeFailure("context transition source metadata lacks exact fields")
        result.append(record)
    return result


def validate_ordinary_packets(log_text, statistics, history):
    from reflection_optical_smoke import SLICE_PACKET_FIELDS
    observations = records(log_text, "ReflectionSmokeSlicePackets:", SLICE_PACKET_FIELDS)
    by_source = {record["source_frame"]: record for record in observations}
    if len(by_source) != len(observations):
        raise SmokeFailure("duplicate ordinary packet source frame")
    sources = {(item["sequence"], item["generation"]): item["graphics_frame"] for item in history}
    completed = []
    for sample in statistics:
        observation = by_source.get(sources[(sample["sequence"], sample["generation"])])
        if observation is None or observation["runtime_present"] != 1 or not observation["plan_generation"]:
            raise SmokeFailure("ordinary phase lacks current accepted hardware packet evidence")
        if observation["device_generation"] != sample["device_generation"] or observation["graphics_queue"] >= 65535:
            raise SmokeFailure("ordinary hardware packet has a stale device identity")
        if observation["ray_capacity"] != min(sample["effective_budget"], sample["queue_capacity"]):
            raise SmokeFailure("ordinary packet describes a different frozen ray capacity")
        if any(observation[name] != 1 for name in ("expected_slices", "hardware_nodes", "indexed_slices",
            "compiled_nodes", "unique_packets", "accepted_packets")):
            raise SmokeFailure("ordinary phase must execute one accepted hardware task without stale CSG slices")
        if observation["graphics_packets"] != 1:
            raise SmokeFailure("ordinary matched control must retain its accepted primary Graphics hardware packet")
        completed.append(observation)
    return completed


def analyze_evidence(log_text):
    begin_lines = "\n".join(line for line in log_text.splitlines() if "ReflectionCsgContext: phase=" in line)
    capture_lines = "\n".join(line for line in log_text.splitlines() if "ReflectionCsgContext: captured " in line)
    starts = records(begin_lines, "ReflectionCsgContext: ", PHASE_FIELDS)
    captures = records(capture_lines, "ReflectionCsgContext: captured ", CAPTURE_FIELDS)
    if len(starts) != 5 or len(captures) != 5 or "ReflectionCsgContext: complete captures=5" not in log_text:
        raise SmokeFailure("live context transition lacks all five completed captures")
    anchors = list(map(int, re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", log_text)))
    if anchors != [capture["source_frame"] for capture in captures]:
        raise SmokeFailure("context captures are detached from actual framebuffer source frames")
    history = parse_history(log_text)
    statistics = parse_statistics(log_text)
    optics = parse_optics(log_text)
    histories = {(item["sequence"], item["generation"]): item for item in history}
    if len(histories) != len(history):
        raise SmokeFailure("duplicate completed context source identity")
    stats_keys = {(item["sequence"], item["generation"]) for item in statistics}
    optics_keys = {(item["sequence"], item["generation"]) for item in optics}
    if len(stats_keys) != len(statistics) or len(optics_keys) != len(optics) or stats_keys != set(histories) or stats_keys != optics_keys:
        raise SmokeFailure("context counters do not share exact completed source identities")
    evidence = []
    for phase, (name, case, cut_receiver_primitives, csg) in enumerate(PHASES):
        start, capture = starts[phase], captures[phase]
        end = starts[phase + 1]["source_frame"] if phase < 4 else math.inf
        if start["phase"] != phase or capture["phase"] != phase or start["cut_receiver_primitives"] != cut_receiver_primitives or start["csg"] != csg:
            raise SmokeFailure("context transition changed the requested current receiver admission")
        if not start["source_frame"] <= capture["source_frame"] < end or capture["completed_frames"] < 16:
            raise SmokeFailure("context transition captured before its completed-frame qualification")
        selected = [sample for sample in statistics if sample["frame"] >= 3
            and start["source_frame"] <= histories[(sample["sequence"], sample["generation"])]["graphics_frame"] < end]
        keys = {(sample["sequence"], sample["generation"]) for sample in selected}
        selected_history = [item for item in history if (item["sequence"], item["generation"]) in keys]
        if sum(item["graphics_frame"] < capture["source_frame"] for item in selected_history) < 16:
            raise SmokeFailure("context image lacks sixteen prior completed accepted frames in its own phase")
        if not any(item["graphics_frame"] == capture["source_frame"] for item in selected_history):
            raise SmokeFailure("context image lacks completed metadata for its exact captured source frame")
        lines = []
        for line in log_text.splitlines():
            if any(marker in line for marker in ("ReflectionSmokeStatistics:", "ReflectionSmokeHistory:", "ReflectionSmokeOptics:")):
                identity = re.search(r"sequence=([0-9]+) generation=([0-9]+)", line)
                if identity and tuple(map(int, identity.groups())) in keys:
                    lines.append(line)
            elif "ReflectionSmokeSlicePackets:" in line:
                source = re.search(r"source_frame=([0-9]+)", line)
                if source and start["source_frame"] <= int(source[1]) < end:
                    lines.append(line)
        lines.append("FramebufferCapture: graphics source frame " + str(capture["source_frame"]))
        phase_log = "\n".join(lines)
        spec = OpticalCapture(name, case)
        result = validate_optics(phase_log, spec)
        result["capture_source"] = validate_history(phase_log, selected,
            CaptureSpec(name, case=case, roughness=0, samples=16, temporal=False))
        if not csg:
            result["ordinary_packets"] = validate_ordinary_packets(phase_log, selected, selected_history)
        if cut_receiver_primitives == 12:
            by_key = {(item["sequence"], item["generation"]): item for item in result["optics"]}
            for sample in selected:
                optical = by_key[(sample["sequence"], sample["generation"])]
                rays = sample["hardware_rays"]
                if (not rays or rays != min(sample["candidates"], sample["effective_budget"])
                    or sample["hardware_hits"] != rays or sample["opaque_pixels"] != rays or sample["glass_pixels"]
                    or sample["fallback_pixels"] or optical["transparent_paths"] != rays
                    or optical["query_budget_units"] != 11 * rays or optical["physical_queries"] != 7 * rays
                    or optical["bootstrap_events"]
                    or any(optical[field] for field in ("unsupported_paths", "limited_paths", "ambiguous_paths",
                        "tir_events", "medium_overflow_paths"))):
                    raise SmokeFailure("small cap phase lost exact completed rays, interface queries or resolved transport")
        evidence.append({"phase": phase, "name": name, "case": case, "begin": start, "capture": capture, **result})
    return evidence


def analyze_images(directory, evidence):
    frames, metrics = [], {}
    for item in evidence:
        phase = item["phase"]
        path = directory / ("context_transition.bmp" if phase == 4 else f"context_transition.bmp.{phase}.bmp")
        frame = read_bmp_24_rows(path)
        frames.append(frame)
        metrics[item["name"]] = {"image": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            **analyze_image(frame, OpticalCapture(item["name"], item["case"]))}
    for first, second in ((0, 3), (1, 2), (1, 4)):
        metrics[f"phase_{first}_equals_{second}"] = compare_invariant(frames[first], frames[second])
    return metrics


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    directory = args.output_directory.resolve()
    if directory.exists() and any(directory.iterdir()):
        parser.error("output directory must be empty; completed evidence is never overwritten")
    directory.mkdir(parents=True, exist_ok=True)
    output, log = directory / "context_transition.bmp", directory / "runtime.log"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), "--log-output", str(log)]
    for message in ("ReflectionSmokeProject: case " + CASE + " created", "ReflectionSmokeProject: hardware available",
        "ReflectionSmokeProject: reflection mode hardware", "Reflection resolve: hardware",
        "ReflectionCsgContext: complete captures=5", "ReflectionSmokeProject: shutdown"):
        command += ["--expect-log-message", message]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    environment = capture_environment(CASE, "hardware", DEFAULT_RAY_BUDGET)
    environment.update({"NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": "16", "NWB_REFLECTION_SMOKE_TEMPORAL": "0",
        "NWB_REFLECTION_SMOKE_SPATIAL": "0", "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": "16",
        "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1"})
    try:
        result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + 90)
        if result.returncode == SKIP_EXIT_CODE:
            return SKIP_EXIT_CODE
        if result.returncode:
            raise SmokeFailure("live CSG context capture failed with exit " + str(result.returncode))
        evidence = analyze_evidence(log.read_text(encoding="utf-8"))
        manifest = {"frame_source": "actual application framebuffer readback", "size": [960, 720],
            "environment_overrides": {name: environment[name] for name in environment if name.startswith("NWB_")},
            "completed_phase_evidence": evidence, "metrics": None,
            "diagnostic_sampling": "The owning probe samples each fixture frame after public graphics waitForIdle, retaining exact-source latest-only statistics.",
            "limitations": "Five live context transitions qualify HW reflection semantics and accepted packet coverage with synchronized frames; no asynchronous-overlap, performance or GI claim."}
        report = directory / "manifest.json"
        report.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        manifest["metrics"] = analyze_images(directory, evidence)
        report.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print("PASS: live small/dense/ordinary CSG contexts, exact accepted source frames, packet coverage and image invariance", flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
