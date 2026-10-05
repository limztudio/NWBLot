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

LIT_PHASE = "Phase"
LIT_INDEX = "index"
LIT_RADIUS = "radius"
LIT_SEED = "seed"
LIT_GRAPHICS_FRAME = "graphics_frame"
LIT_WARM = "Warm"
LIT_SEQUENCE = "sequence"
LIT_GENERATION = "generation"
LIT_EPOCH = "epoch"
LIT_START_GRAPHICS_FRAME = "start_graphics_frame"
LIT_SAMPLE_INDEX = "sample_index"
LIT_WORK_PUBLISH = "work_publish"
LIT_WORK_FIRST = "work_first"
LIT_WORK_LAST = "work_last"
LIT_WORK_SAMPLES = "work_samples"
LIT_WORK = "Work"
LIT_PUBLISH = "publish"
LIT_FIRST = "first"
LIT_LAST = "last"
LIT_SAMPLES = "samples"
LIT_CAPTURE = "Capture"
LIT_GPUDBG = "--gpudbg"
LIT_GPUDBG_2 = "--gpudbg="
LIT_HARDWARE_READY = "hardware_ready"
LIT_COUNT = "count"
LIT_ELIGIBLE = "eligible"
LIT_EXACT_PAIRS = "exact_pairs"
LIT_RADIUS_DISCRIMINATION = "radius_discrimination"
LIT_FRESH = "fresh"
LIT_ROUGH = "rough"
LIT_HARDWARE = "hardware"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_UTF_8 = "utf-8"
LIT_STATUS = "status"
LIT_CAPTURES = "captures"
LIT_QUALIFICATION_ARM_OR_SHARED_INPUT_CHAN = "qualification arm or shared input changed"
LIT_PASSED = "passed"
LIT_MAIN = "__main__"
LIT_APPEND = "append"


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
    LIT_PHASE: {LIT_INDEX, LIT_RADIUS, LIT_SEED, LIT_GRAPHICS_FRAME},
    LIT_WARM: {LIT_INDEX, LIT_SEQUENCE, LIT_GENERATION, LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME,
        LIT_SAMPLE_INDEX, LIT_SEED, LIT_WORK_PUBLISH, LIT_WORK_FIRST, LIT_WORK_LAST, LIT_WORK_SAMPLES},
    LIT_WORK: {LIT_PUBLISH, LIT_FIRST, LIT_LAST, LIT_SAMPLES},
    LIT_CAPTURE: {LIT_RADIUS, LIT_GRAPHICS_FRAME, LIT_SEED},
}


def validate_gpu_debug(text, application_args):
    requested = any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in application_args)
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
    phases, warmed, windows, captures = (records(text, kind) for kind in (LIT_PHASE, LIT_WARM, LIT_WORK, LIT_CAPTURE))
    if len(phases) != len(expected) or len(warmed) != len(expected) or len(captures) != 1:
        raise SmokeFailure("owner qualification lacks every requested phase, warm proof or final reset")
    announced = re.findall(r"ReflectionSpatialOwner: selection=([a-z0-9]+) initial_radius=([0-9]+) final_radius=([0-9]+) phases=([0-9]+)", text)
    if announced != [(selection, str(expected[0]), str(expected[-1]), str(len(expected)))]:
        raise SmokeFailure("actual owner settings do not match the requested sequence")
    anchors = re.findall(r"FramebufferCapture: graphics source frame ([0-9]+)", text)
    capture = captures[0]
    if anchors != [str(capture[LIT_GRAPHICS_FRAME])] or capture[LIT_RADIUS] != expected[-1] or capture[LIT_SEED] != 0:
        raise SmokeFailure("framebuffer is detached from the final radius/seed source frame")
    by_history = {(item[LIT_SEQUENCE], item[LIT_GENERATION]): item for item in history}
    by_statistics = {(item[LIT_SEQUENCE], item[LIT_GENERATION]): item for item in statistics}
    if len(by_history) != len(history) or len(by_statistics) != len(statistics):
        raise SmokeFailure("duplicate completed sequence identity")
    if any(key not in by_statistics for key in by_history):
        raise SmokeFailure("history has no matching completed queue-token evidence")
    if len({item[LIT_GENERATION] for item in history}) != 1 or len({item["device_generation"] for item in statistics}) != 1:
        raise SmokeFailure("owner evidence crosses resource/device generations")
    by_publish = {}
    last_publish = -1
    for work in windows:
        if work[LIT_PUBLISH] <= last_publish or work[LIT_SAMPLES] <= 0 or work[LIT_FIRST] > work[LIT_LAST] \
                or work[LIT_SAMPLES] > work[LIT_LAST] - work[LIT_FIRST] + 1:
            raise SmokeFailure("invalid or replayed completed spatial window")
        by_publish[work[LIT_PUBLISH]] = work
        last_publish = work[LIT_PUBLISH]
    if not windows:
        raise SmokeFailure("no actual completed spatial work")
    previous_warm = None
    for index, (phase, warm, radius) in enumerate(zip(phases, warmed, expected)):
        if phase != {LIT_INDEX: index, LIT_RADIUS: radius, LIT_SEED: 101 + index,
                LIT_GRAPHICS_FRAME: phase[LIT_GRAPHICS_FRAME]} or warm[LIT_INDEX] != index:
            raise SmokeFailure("missing, reordered or wrong-radius owner phase")
        if previous_warm is not None and (phase[LIT_GRAPHICS_FRAME] <= previous_warm[LIT_GRAPHICS_FRAME]
                or warm[LIT_EPOCH] <= previous_warm[LIT_EPOCH]):
            raise SmokeFailure("radius changed before the preceding phase completed")
        key = (warm[LIT_SEQUENCE], warm[LIT_GENERATION])
        sample, counters = by_history.get(key), by_statistics.get(key)
        if sample is None or counters is None or not counters[LIT_HARDWARE_READY]:
            raise SmokeFailure("warm phase lacks completed hardware/history evidence")
        for field in (LIT_GRAPHICS_FRAME, LIT_EPOCH, LIT_START_GRAPHICS_FRAME, LIT_SAMPLE_INDEX, LIT_SEED):
            if sample[field] != warm[field]:
                raise SmokeFailure("warm phase is detached from its completed history record")
        if sample[LIT_START_GRAPHICS_FRAME] != phase[LIT_GRAPHICS_FRAME] or sample[LIT_SEED] != phase[LIT_SEED] \
                or sample[LIT_SAMPLE_INDEX] < 2 or sample[LIT_COUNT] != 1 or sample[LIT_ELIGIBLE] != 1:
            raise SmokeFailure("phase did not complete three accepted samples from its exact reset boundary")
        work = by_publish.get(warm[LIT_WORK_PUBLISH])
        expected_work = {LIT_PUBLISH: warm[LIT_WORK_PUBLISH], LIT_FIRST: warm[LIT_WORK_FIRST],
            LIT_LAST: warm[LIT_WORK_LAST], LIT_SAMPLES: warm[LIT_WORK_SAMPLES]}
        if work != expected_work or work[LIT_FIRST] < phase[LIT_GRAPHICS_FRAME] or work[LIT_LAST] > sample[LIT_GRAPHICS_FRAME]:
            raise SmokeFailure("warm phase spatial work is missing, stale or not completed")
        previous_warm = warm
    source = capture[LIT_GRAPHICS_FRAME]
    if source <= previous_warm[LIT_GRAPHICS_FRAME]:
        raise SmokeFailure("capture reset precedes completion of the last prepared radius")
    coverage = [work for work in windows if work[LIT_FIRST] <= source <= work[LIT_LAST]
        and work[LIT_SAMPLES] == work[LIT_LAST] - work[LIT_FIRST] + 1]
    if not coverage:
        raise SmokeFailure("captured source has no dense completed spatial-range coverage")
    covering = [sample for sample in history if sample[LIT_START_GRAPHICS_FRAME] == source
        and sample[LIT_GRAPHICS_FRAME] >= max(source, coverage[0][LIT_LAST]) and sample[LIT_SEED] == 0
        and sample[LIT_ELIGIBLE] == 1 and sample[LIT_COUNT] == 1 and sample[LIT_EPOCH] > previous_warm[LIT_EPOCH]
        and by_statistics[(sample[LIT_SEQUENCE], sample[LIT_GENERATION])][LIT_HARDWARE_READY]]
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
    result = {LIT_EXACT_PAIRS: {}, LIT_RADIUS_DISCRIMINATION: {}}
    for radius in (1, 2, 3):
        fresh, sequence = frames[LIT_FRESH + str(radius)], frames[LIT_SEQUENCE + str(radius)]
        if fresh != sequence:
            raise SmokeFailure("radius transition differs from the matching fresh first-sample framebuffer")
        result[LIT_EXACT_PAIRS][str(radius)] = {"rgb_byte_identical": True, "pixels": width * height}
    cells = mirror_cells(width, height)
    left, top = cells[0][:2]
    right, bottom = cells[-1][2:]
    area = (right - left) * (bottom - top)
    if area <= 0:
        raise SmokeFailure("empty projected mirror footprint")
    for first, second in ((1, 2), (1, 3), (2, 3)):
        a, b = frames[LIT_FRESH + str(first)][2], frames[LIT_FRESH + str(second)][2]
        changed = sum(max(abs(x - y) for x, y in zip(a[row][column], b[row][column])) > 2
            for row in range(top, bottom) for column in range(left, right))
        minimum = max(32, int(area * .001))
        if changed < minimum:
            raise SmokeFailure("fresh radius references do not discriminate wrong-radius bank selection")
        result[LIT_RADIUS_DISCRIMINATION][str(first) + "_" + str(second)] = {
            "projected_mirror_pixels": area, "pixels_differing_by_more_than_two": changed,
            "minimum_discriminating_pixels": minimum}
    return result


def capture_environment_for(selection):
    env = capture_environment(LIT_ROUGH, LIT_HARDWARE)
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
        LIT_TIMEOUT, str(args.timeout), "--log-output", str(log)]
    for expected in ("ReflectionSmokeProject: case rough created", "ReflectionSmokeProject: shutdown",
            "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: reflection mode hardware",
            "Reflection resolve: hardware", "ReflectionSpatialOwnerCapture:", "ReflectionSpatialOwnerWork:"):
        command += [LIT_EXPECT_LOG_MESSAGE, expected]
    if any(value == LIT_GPUDBG or value.startswith(LIT_GPUDBG_2) for value in args.application_arg):
        for marker in GPU_DEBUG_MARKERS:
            command += [LIT_EXPECT_LOG_MESSAGE, marker]
    command.extend("--application-arg=" + value for value in args.application_arg)
    print("Capturing production spatial owner " + selection, flush=True)
    result = subprocess.run(command, env=capture_environment_for(selection), check=False, timeout=args.timeout + 90)
    if result.returncode:
        raise SmokeFailure(selection + " capture failed or skipped: " + str(result.returncode))
    frame = read_bmp_24_rows(output)
    validate_frame(frame)
    if frame[:2] != (960, 720):
        raise SmokeFailure("owner capture has the wrong extent")
    text = log.read_text(encoding=LIT_UTF_8)
    gpu_debug = validate_gpu_debug(text, args.application_arg)
    statistics = parse_statistics(text)
    validate_statistics(statistics, LIT_ROUGH, LIT_HARDWARE, allow_zero_samples=True)
    evidence = validate_owner_evidence(text, selection, parse_history(text), statistics)
    return frame, {"selection": selection, "image": file_identity(output), "log": file_identity(log),
        "command": command, "evidence": evidence, "gpu_debug": gpu_debug}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    for name in ("executable", "working-directory", "logserver-executable", "source-manifest", "output-directory"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument(LIT_TIMEOUT, type=float, default=60)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
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
    report = {LIT_STATUS: "incomplete", "identity": identity, "shared_inputs": shared, "logserver_binaries": logger_identity, LIT_CAPTURES: []}
    try:
        frames = {}
        for selection in SELECTIONS:
            if ab.freeze_arm(arm) != identity or ab.binary_identity(args.logserver_executable) != logger_identity \
                    or any(file_identity(Path(path)) != saved for path, saved in shared.items()):
                raise SmokeFailure(LIT_QUALIFICATION_ARM_OR_SHARED_INPUT_CHAN)
            frame, evidence = capture(args, selection)
            frames[selection] = frame
            report[LIT_CAPTURES].append(evidence)
        if ab.freeze_arm(arm) != identity or ab.binary_identity(args.logserver_executable) != logger_identity \
                or any(file_identity(Path(path)) != saved for path, saved in shared.items()):
            raise SmokeFailure(LIT_QUALIFICATION_ARM_OR_SHARED_INPUT_CHAN)
        report["images"] = compare_images(frames)
        report[LIT_STATUS] = LIT_PASSED
    except (SmokeFailure, OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print("FAIL: " + str(error), file=sys.stderr)
    finally:
        (args.output_directory / "owner_qualification.json").write_text(
            json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding=LIT_UTF_8)
    if report[LIT_STATUS] != LIT_PASSED:
        return 1
    print("PASS: six production-owner captures, accepted phases and exact fresh/transition pixels")
    return 0


if __name__ == LIT_MAIN:
    raise SystemExit(main())
