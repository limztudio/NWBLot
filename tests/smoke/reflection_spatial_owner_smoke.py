#!/usr/bin/env python3
"""Six actual-framebuffer comparisons for the production reflection spatial-radius owner."""

import argparse
import json
import re
from pathlib import Path
import subprocess
import sys

import renderer_ab_benchmark as ab
from reflection_smoke import (capture_environment, parse_statistics, validate_frame, validate_statistics)
from reflection_roughness_smoke import parse_history
from reflection_roughness_reference import mirror_cells
from smoke_volume_identity import file_identity
from window_capture_smoke import SmokeFailure, read_bmp_24_rows


SELECTIONS = {
    "fresh1": (1,), "fresh2": (2,), "fresh3": (3,),
    "sequence3": (1, 3), "sequence2": (1, 3, 2), "sequence1": (1, 3, 2, 1),
}
GPU_DEBUG_MARKERS = (
    "Loader: GPU debug validation enabled",
    "validation layer enabled: yes",
    "Vulkan GPU debug: debug utils messenger installed.",
)
FIELDS = {
    "Phase": {"index", "radius", "seed", "graphics_frame"},
    "Warm": {"index", "sequence", "generation", "graphics_frame", "epoch", "start_graphics_frame",
        "sample_index", "seed", "work_publish", "work_first", "work_last", "work_samples"},
    "Work": {"publish", "first", "last", "samples"},
    "Capture": {"radius", "graphics_frame", "seed"},
}


def validate_gpu_debug(text, application_args):
    requested = any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in application_args)
    if requested:
        lines = [line.strip() for line in text.splitlines()]
        if any(lines.count(marker) != 1 for marker in GPU_DEBUG_MARKERS):
            raise SmokeFailure("requested GPU validation lacks its actual loader/layer/messenger markers")
    return {"requested": requested, "markers": list(GPU_DEBUG_MARKERS) if requested else []}


def records(text, kind):
    result = []
    prefix = "ReflectionSpatialOwner" + kind + ":"
    for line in text.splitlines():
        if prefix not in line:
            continue
        item = {}
        for field in line.split(prefix, 1)[1].split():
            match = re.fullmatch(r"([a-z_]+)=([0-9]+)", field)
            if not match or match[1] in item:
                raise SmokeFailure("malformed or duplicate owner evidence field")
            item[match[1]] = int(match[2])
        if set(item) != FIELDS[kind]:
            raise SmokeFailure("missing or unexpected owner evidence field")
        result.append(item)
    return result


def validate_owner_evidence(text, selection, history, statistics):
    expected = SELECTIONS[selection]
    phases, warmed, windows, captures = (records(text, kind) for kind in ("Phase", "Warm", "Work", "Capture"))
    if len(phases) != len(expected) or len(warmed) != len(expected) or len(captures) != 1:
        raise SmokeFailure("owner qualification lacks every requested phase, warm proof or final reset")
    announced = re.findall(r"ReflectionSpatialOwner: selection=([a-z0-9]+) initial_radius=([0-9]+) final_radius=([0-9]+) phases=([0-9]+)", text)
    if announced != [(selection, str(expected[0]), str(expected[-1]), str(len(expected)))]:
        raise SmokeFailure("actual owner settings do not match the requested sequence")
    anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", text)
    capture = captures[0]
    if anchors != [str(capture["graphics_frame"])] or capture["radius"] != expected[-1] or capture["seed"] != 0:
        raise SmokeFailure("framebuffer is detached from the final radius/seed source frame")
    by_history = {(item["sequence"], item["generation"]): item for item in history}
    by_statistics = {(item["sequence"], item["generation"]): item for item in statistics}
    if len(by_history) != len(history) or len(by_statistics) != len(statistics):
        raise SmokeFailure("duplicate completed sequence identity")
    if any(key not in by_statistics for key in by_history):
        raise SmokeFailure("history has no matching completed queue-token evidence")
    if len({item["generation"] for item in history}) != 1 or len({item["device_generation"] for item in statistics}) != 1:
        raise SmokeFailure("owner evidence crosses resource/device generations")
    by_publish = {}
    last_publish = -1
    for work in windows:
        if work["publish"] <= last_publish or work["samples"] <= 0 or work["first"] > work["last"] \
                or work["samples"] > work["last"] - work["first"] + 1:
            raise SmokeFailure("invalid or replayed completed spatial window")
        by_publish[work["publish"]] = work
        last_publish = work["publish"]
    if not windows:
        raise SmokeFailure("no actual completed spatial work")
    previous_warm = None
    for index, (phase, warm, radius) in enumerate(zip(phases, warmed, expected)):
        if phase != {"index": index, "radius": radius, "seed": 101 + index,
                "graphics_frame": phase["graphics_frame"]} or warm["index"] != index:
            raise SmokeFailure("missing, reordered or wrong-radius owner phase")
        if previous_warm is not None and (phase["graphics_frame"] <= previous_warm["graphics_frame"]
                or warm["epoch"] <= previous_warm["epoch"]):
            raise SmokeFailure("radius changed before the preceding phase completed")
        key = (warm["sequence"], warm["generation"])
        sample, counters = by_history.get(key), by_statistics.get(key)
        if sample is None or counters is None or not counters["hardware_ready"]:
            raise SmokeFailure("warm phase lacks completed hardware/history evidence")
        for field in ("graphics_frame", "epoch", "start_graphics_frame", "sample_index", "seed"):
            if sample[field] != warm[field]:
                raise SmokeFailure("warm phase is detached from its completed history record")
        if sample["start_graphics_frame"] != phase["graphics_frame"] or sample["seed"] != phase["seed"] \
                or sample["sample_index"] < 2 or sample["count"] != 1 or sample["eligible"] != 1:
            raise SmokeFailure("phase did not complete three accepted samples from its exact reset boundary")
        work = by_publish.get(warm["work_publish"])
        expected_work = {"publish": warm["work_publish"], "first": warm["work_first"],
            "last": warm["work_last"], "samples": warm["work_samples"]}
        if work != expected_work or work["first"] < phase["graphics_frame"] or work["last"] > sample["graphics_frame"]:
            raise SmokeFailure("warm phase spatial work is missing, stale or not completed")
        previous_warm = warm
    source = capture["graphics_frame"]
    if source <= previous_warm["graphics_frame"]:
        raise SmokeFailure("capture reset precedes completion of the last prepared radius")
    coverage = [work for work in windows if work["first"] <= source <= work["last"]
        and work["samples"] == work["last"] - work["first"] + 1]
    if not coverage:
        raise SmokeFailure("captured source has no dense completed spatial-range coverage")
    covering = [sample for sample in history if sample["start_graphics_frame"] == source
        and sample["graphics_frame"] >= max(source, coverage[0]["last"]) and sample["seed"] == 0
        and sample["eligible"] == 1 and sample["count"] == 1 and sample["epoch"] > previous_warm["epoch"]
        and by_statistics[(sample["sequence"], sample["generation"])]["hardware_ready"]]
    if not covering:
        raise SmokeFailure("captured image is not the first frame of its completed seed-zero epoch")
    return {"radii": list(expected), "phases": phases, "warm": warmed,
        "captured_graphics_frame": source, "capture_work": coverage[0], "covering_history": covering[0],
        "completed_spatial_windows": windows}


def compare_images(frames):
    if set(frames) != set(SELECTIONS):
        raise SmokeFailure("all six fresh/sequence captures are required")
    sizes = {frame[:2] for frame in frames.values()}
    if len(sizes) != 1:
        raise SmokeFailure("owner captures have different dimensions")
    width, height = next(iter(sizes))
    result = {"exact_pairs": {}, "radius_discrimination": {}}
    for radius in (1, 2, 3):
        fresh, sequence = frames["fresh" + str(radius)], frames["sequence" + str(radius)]
        if fresh != sequence:
            raise SmokeFailure("radius transition differs from the matching fresh first-sample framebuffer")
        result["exact_pairs"][str(radius)] = {"rgb_byte_identical": True, "pixels": width * height}
    cells = mirror_cells(width, height)
    left, top = cells[0][:2]
    right, bottom = cells[-1][2:]
    area = (right - left) * (bottom - top)
    if area <= 0:
        raise SmokeFailure("empty projected mirror footprint")
    for first, second in ((1, 2), (1, 3), (2, 3)):
        a, b = frames["fresh" + str(first)][2], frames["fresh" + str(second)][2]
        changed = sum(max(abs(x - y) for x, y in zip(a[row][column], b[row][column])) > 2
            for row in range(top, bottom) for column in range(left, right))
        minimum = max(32, int(area * .001))
        if changed < minimum:
            raise SmokeFailure("fresh radius references do not discriminate wrong-radius bank selection")
        result["radius_discrimination"][str(first) + "_" + str(second)] = {
            "projected_mirror_pixels": area, "pixels_differing_by_more_than_two": changed,
            "minimum_discriminating_pixels": minimum}
    return result


def capture_environment_for(selection):
    env = capture_environment("rough", "hardware")
    for suffix, value in {"SPATIAL_OWNER": selection, "ROUGHNESS": ".4", "TEMPORAL": "1",
            "SPATIAL": "1", "HISTORY_SAMPLES": "1", "DIAGNOSTICS": "1", "FEEDBACK": "0",
            "SEED": "99", "EXTENT": "native", "OPTICAL_QUERIES": "16"}.items():
        env["NWB_REFLECTION_SMOKE_" + suffix] = value
    return env


def capture(args, selection):
    output = args.output_directory / (selection + ".bmp")
    log = output.with_suffix(".log")
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--logserver-executable", str(args.logserver_executable), "--output", str(output),
        "--application-capture", "--application-capture-frame-count", "1",
        "--timeout", str(args.timeout), "--log-output", str(log)]
    for expected in ("ReflectionSmokeProject: case rough created", "ReflectionSmokeProject: shutdown",
            "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: reflection mode hardware",
            "Reflection resolve: hardware", "ReflectionSpatialOwnerCapture:", "ReflectionSpatialOwnerWork:"):
        command += ["--expect-log-message", expected]
    if any(value == "--gpudbg" or value.startswith("--gpudbg=") for value in args.application_arg):
        for marker in GPU_DEBUG_MARKERS:
            command += ["--expect-log-message", marker]
    command.extend("--application-arg=" + value for value in args.application_arg)
    print("Capturing production spatial owner " + selection, flush=True)
    result = subprocess.run(command, env=capture_environment_for(selection), check=False, timeout=args.timeout + 90)
    if result.returncode:
        raise SmokeFailure(selection + " capture failed or skipped: " + str(result.returncode))
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("owner capture has the wrong extent")
    text = log.read_text(encoding="utf-8")
    gpu_debug = validate_gpu_debug(text, args.application_arg)
    statistics = parse_statistics(text)
    validate_statistics(statistics, "rough", "hardware", allow_zero_samples=True)
    evidence = validate_owner_evidence(text, selection, parse_history(text), statistics)
    return frame, {"selection": selection, "image": file_identity(output), "log": file_identity(log),
        "command": command, "evidence": evidence, "gpu_debug": gpu_debug}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for name in ("executable", "working-directory", "logserver-executable", "source-manifest", "output-directory"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not 0 < args.timeout <= 180:
        parser.error("timeout must be positive and at most180 seconds per capture")
    for key, value in vars(args).copy().items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    if args.output_directory.exists():
        parser.error("output directory must be new")
    for protected in (args.executable.parent, args.working_directory,
            args.source_manifest.parent, args.logserver_executable.parent):
        if args.output_directory == protected or args.output_directory.is_relative_to(protected) \
                or protected.is_relative_to(args.output_directory):
            parser.error("output must not overlap frozen source, binary, runtime or logger inputs")
    arm = ab.Arm("qualification", args.executable, args.working_directory, args.source_manifest)
    identity = ab.freeze_arm(arm)
    shared = {str(Path(module.__file__).resolve()): file_identity(Path(module.__file__))
        for module in (sys.modules[__name__], ab, sys.modules["reflection_smoke"],
            sys.modules["reflection_roughness_smoke"], sys.modules["reflection_roughness_reference"],
            sys.modules["window_capture_smoke"], sys.modules["smoke_volume_identity"])}
    logger_identity = ab.binary_identity(args.logserver_executable)
    for name, saved in logger_identity.items():
        shared[str(args.logserver_executable.parent / name)] = saved
    shared[str(Path(sys.executable).resolve())] = file_identity(Path(sys.executable))
    args.output_directory.mkdir(parents=True)
    report = {"status": "incomplete", "identity": identity, "shared_inputs": shared, "logserver_binaries": logger_identity, "captures": []}
    try:
        frames = {}
        for selection in SELECTIONS:
            if ab.freeze_arm(arm) != identity or ab.binary_identity(args.logserver_executable) != logger_identity \
                    or any(file_identity(Path(path)) != saved for path, saved in shared.items()):
                raise SmokeFailure("qualification arm or shared input changed")
            frame, evidence = capture(args, selection)
            frames[selection] = frame
            report["captures"].append(evidence)
        if ab.freeze_arm(arm) != identity or ab.binary_identity(args.logserver_executable) != logger_identity \
                or any(file_identity(Path(path)) != saved for path, saved in shared.items()):
            raise SmokeFailure("qualification arm or shared input changed")
        report["images"] = compare_images(frames)
        report["status"] = "passed"
    except (SmokeFailure, OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print("FAIL: " + str(error), file=sys.stderr)
    finally:
        (args.output_directory / "owner_qualification.json").write_text(
            json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    if report["status"] != "passed":
        return 1
    print("PASS: six production-owner captures, accepted phases and exact fresh/transition pixels")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
