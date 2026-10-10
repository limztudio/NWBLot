#!/usr/bin/env python3
"""Capture actual CPU/GPU publications for matched ordinary/CSG slabs; diagnostic evidence, not FPS qualification."""

import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys
from types import SimpleNamespace

import renderer_ab_benchmark as ab
import reflection_benchmark as reflection
import smoke_cpu_gpu_timing as timing
from smoke_volume_identity import file_identity
from window_capture_smoke import (SmokeFailure, SmokeSkip, STRICT_LOG_FAILURE_MESSAGES, build_launch_environment,
    launch_logserver, launch_testbed, require_normal_process_exit, shutdown_logserver_and_collect,
    terminate_process, validate_expected_log_text)

CASES = ("optical_csg_reference", "optical_csg_cap")
REQUIRED_GPU = ("render.frame", "render.reflection_hardware")
REQUIRED_CPU = ("graphics.frame", "graphics.prepare_resources", "graphics.render_passes")
MINIMUM_INTERVALS = 11
MINIMUM_SAMPLES = 100
MINIMUM_SECONDS = 30.0
RAW_NAME = "cpu_gpu_timing.txt"
NUMBER = r"[^ ]+"


def _duration(text):
    try:
        value = float(text)
    except ValueError as error:
        raise SmokeFailure("reflection diagnostic duration is malformed") from error
    if not math.isfinite(value) or value <= 0:
        raise SmokeFailure("reflection diagnostic duration must be finite and positive")
    return value


def _one(lines, prefix, pattern):
    matches = [line for line in lines if line.startswith(prefix)]
    if len(matches) != 1 or not (match := re.fullmatch(pattern, matches[0])):
        raise SmokeFailure(f"reflection diagnostic needs one valid {prefix} record")
    return match, lines.index(matches[0])


def parse_runtime_log(text, case):
    if case not in CASES:
        raise SmokeFailure("unsupported reflection CPU diagnostic fixture")
    validate_expected_log_text(text, [f"ReflectionSmokeProject: case {case} created",
        "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: shutdown"],
        [*STRICT_LOG_FAILURE_MESSAGES, "FramebufferCapture:", "ReflectionSmokeStatistics:",
         "ReflectionSmokeHistory:", "ReflectionSmokeOptics:", "ReflectionSmokeSlicePackets:",
         "ReflectionSmokeProject: fps avg=", "render submission suspended"])
    args = SimpleNamespace(family=case, ray_budget=1382400, optical_queries=16, screen_steps=96, mip_count=10, width=960, height=720, require_hardware=True)
    reflection.validate_trial_log(text, args, reflection.Variant("diagnostic", "hardware", temporal=False, spatial=False, feedback=False))
    lines = text.replace("\r\n", "\n").splitlines()
    extents = set(ab.DIMENSIONS.findall(text))
    if extents != {("960", "720")}:
        raise SmokeFailure("reflection diagnostic extent differs from 960x720")
    availability = [line for line in lines if line.startswith("ReflectionSmokeProject: hardware ")
        and line.endswith((" available", " unavailable"))]
    if availability != ["ReflectionSmokeProject: hardware available"]:
        raise SmokeFailure("reflection diagnostic has contradictory hardware availability")
    configured, configured_index = _one(lines, "ReflectionCpuTiming: configured ",
        re.escape(f"ReflectionCpuTiming: configured case={case} preparation_presentations=64 warmup_seconds=5 "
        "minimum_seconds=30 minimum_presentations=100 minimum_positive_intervals=11 diagnostic_only=1"))
    preparation, preparation_index = _one(lines, "ReflectionCpuTiming: preparation complete ",
        r"ReflectionCpuTiming: preparation complete presentations=(\d+) source_frame=(\d+)")
    begin, begin_index = _one(lines, "ReflectionCpuTiming: measurement begin ",
        r"ReflectionCpuTiming: measurement begin presentations=(\d+) source_frame=(\d+) warmup_seconds=(" + NUMBER + r")")
    end, end_index = _one(lines, "ReflectionCpuTiming: measurement complete ",
        r"ReflectionCpuTiming: measurement complete presentations=(\d+) seconds=(" + NUMBER + r") first=(\d+) last=(\d+) "
        r"positive_intervals=(\d+) end_source=(\d+) diagnostic_only=1")
    if not configured_index < preparation_index < begin_index < end_index:
        raise SmokeFailure("reflection diagnostic lifecycle order is invalid")
    prepared, prepared_source = map(int, preparation.groups())
    first, source_first = map(int, begin.groups()[:2])
    warmup = _duration(begin[3])
    count, seconds, end_first, last, positive, source_end = end.groups()
    count, end_first, last, positive, source_end = map(int, (count, end_first, last, positive, source_end))
    seconds = _duration(seconds)
    if prepared < 64 or first < prepared or source_first < prepared_source or warmup < 5.0:
        raise SmokeFailure("reflection diagnostic preparation or actual warmup is insufficient")
    if end_first != first or count != last - first or count < MINIMUM_SAMPLES or seconds < MINIMUM_SECONDS:
        raise SmokeFailure("reflection diagnostic measurement count or duration is insufficient")
    if source_end <= source_first or positive < MINIMUM_INTERVALS:
        raise SmokeFailure("reflection diagnostic measured source range or intervals are insufficient")
    intervals = []
    previous_presentation, previous_source = first, source_first
    for index, line in enumerate(lines):
        if not line.startswith("ReflectionCpuTiming: interval"):
            continue
        match = re.fullmatch(r"ReflectionCpuTiming: interval presentations=(\d+) seconds=(" + NUMBER
            + r") first=(\d+) last=(\d+) first_source=(\d+) end_source=(\d+)", line)
        if match is None or not begin_index < index < end_index:
            raise SmokeFailure("reflection diagnostic interval is malformed or outside measurement")
        admitted, wall, start, finish, frame_start, frame_end = match.groups()
        admitted, start, finish, frame_start, frame_end = map(int, (admitted, start, finish, frame_start, frame_end))
        wall = _duration(wall)
        if (start, frame_start) != (previous_presentation, previous_source) or finish < start or frame_end <= frame_start:
            raise SmokeFailure("reflection diagnostic interval endpoints do not form one continuous window")
        if admitted != finish - start:
            raise SmokeFailure("reflection diagnostic interval count differs from its endpoints")
        intervals.append(dict(presentations=admitted, seconds=wall, first=start, last=finish,
            first_source=frame_start, end_source=frame_end))
        previous_presentation, previous_source = finish, frame_end
    actual_positive = sum(row["presentations"] > 0 for row in intervals)
    if (previous_presentation, previous_source) != (last, source_end) or positive != actual_positive:
        raise SmokeFailure("reflection diagnostic final endpoints or positive interval count disagree")
    if not math.isclose(sum(row["seconds"] for row in intervals), seconds, rel_tol=1e-7, abs_tol=1e-6):
        raise SmokeFailure("reflection diagnostic interval duration sum differs from the actual window")
    return dict(case=case, first=first, last=last, seconds=seconds, presentations=count,
        first_source=source_first, end_source=source_end, warmup_seconds=warmup, preparation_presentations=prepared,
        positive_intervals=positive, intervals=intervals, accepted_wall_frame_ms=1000 * seconds / count,
        diagnostic_only=True, performance_qualification=False, runtime_signature=ab.device_material_signature(text))


def validate_publications(log_text, raw_path, measurement):
    result = timing.verify_capture(log_text, raw_path, measurement, True, "reflection")
    window = result["window"]
    if (window["first_source_frame"], window["end_source_frame_exclusive"]) != (
            measurement["first_source"], measurement["end_source"]):
        raise SmokeFailure("reflection publication source window disagrees with native measurement")
    coverage = {}
    for domain, names in (("cpu", REQUIRED_CPU), ("gpu", REQUIRED_GPU)):
        for name in names:
            rows = [row for row in result["publications"] if row["included"] and row["domain"] == domain and row["scope"] == name]
            if sum(row["samples"] for row in rows) < MINIMUM_SAMPLES:
                raise SmokeFailure(f"reflection diagnostic has fewer than 100 completed {domain} samples: {name}")
            seen_intervals = set()
            last_end = None
            for row in rows:
                if row["seconds"] <= 0:
                    raise SmokeFailure(f"reflection diagnostic target publication is not positive: {name}")
                if last_end is not None and row["first_source_frame"] <= last_end:
                    raise SmokeFailure(f"reflection diagnostic target publication source ranges overlap or regress: {name}")
                if row["samples"] > row["last_source_frame"] - row["first_source_frame"] + 1:
                    raise SmokeFailure(f"reflection diagnostic target scope has more than one sample per source frame: {name}")
                last_end = row["last_source_frame"]
                for index, interval in enumerate(measurement["intervals"]):
                    if interval["presentations"] > 0 and interval["first_source"] < row["observation_frame"] <= interval["end_source"]:
                        seen_intervals.add(index)
                        break
            if len(seen_intervals) < MINIMUM_INTERVALS:
                raise SmokeFailure(f"reflection diagnostic has fewer than 11 positive completed publication intervals: {name}")
            coverage[f"{domain}:{name}"] = dict(positive_publication_intervals=len(seen_intervals),
                completed_samples=sum(row["samples"] for row in rows), first_source=rows[0]["first_source_frame"],
                last_source=rows[-1]["last_source_frame"])
    result["required_publication_coverage"] = coverage
    return result


def launch_environment(base, case, raw_path):
    # Use the existing reflection workload settings; replace its output producer with the explicit CPU/GPU probe.
    workload = ab.reflection_workload("diagnostic", case, "hardware", 0.0, False, False, reflection.HARDWARE)
    env, overrides = ab.configure_environment(base, workload, raw_path)
    env.pop("NWB_GPU_TIMING_FILE", None)
    env.pop("NWB_REFLECTION_CPU_GPU_TIMING_FILE", None)
    env["NWB_REFLECTION_SMOKE_CPU_DIAGNOSTICS"] = "1"
    env["NWB_REFLECTION_CPU_GPU_TIMING_FILE"] = str(raw_path)
    return env


def identities(args):
    helpers = sorted(Path(__file__).resolve().parent.glob("*.py"))
    helpers += sorted((Path(__file__).resolve().parents[1] / "ab").glob("*.py"))
    return dict(renderer=ab.binary_identity(args.executable), logger=ab.binary_identity(args.logserver_executable),
        runtime=ab.runtime_identity(args.working_directory), python=file_identity(Path(sys.executable)),
        helpers={str(path): file_identity(path) for path in helpers})


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def acquire(args):
    output = args.output_directory
    if output.exists() or any(output == protected or output in protected.parents or protected in output.parents
            for protected in (args.working_directory, args.executable.parent, args.logserver_executable.parent)):
        raise SmokeFailure("reflection diagnostic requires a fresh, separate output directory")
    output.mkdir(parents=True)
    before = identities(args)
    env = launch_environment(build_launch_environment(args), args.case, output / RAW_NAME)
    write_json(output / "launch.json", dict(argv=sys.argv, application_args=args.application_arg,
        executable=str(args.executable), runtime=str(args.working_directory), case=args.case,
        environment={key: value for key, value in env.items() if key.startswith(("NWB_", "VK_"))}, identity_before=before))
    process = logger = log_directory = None
    baseline, pattern, collected = {}, "", False
    try:
        logger, port, log_directory, baseline, pattern = launch_logserver(args, args.executable, env)
        process = launch_testbed(args, args.executable, env, port)
        try:
            process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            raise SmokeFailure("reflection CPU diagnostic did not self-exit before its deadline") from error
        code, tail = terminate_process(process, "reflection CPU diagnostic")
        process = None
        (output / "process_tail.txt").write_text(tail, encoding="utf-8")
        require_normal_process_exit(code, tail, "reflection CPU diagnostic")
        text = shutdown_logserver_and_collect(logger, log_directory, baseline, pattern)
        logger, collected = None, True
        (output / "runtime.log").write_text(text, encoding="utf-8")
        measurement = parse_runtime_log(text, args.case)
        publications = validate_publications(text, output / RAW_NAME, measurement)
        write_json(output / "cpu_gpu_summary.json", publications)
        after = identities(args)
        if before != after:
            raise SmokeFailure("reflection diagnostic executable, dependencies, resources or Python closure changed")
        result = dict(schema=1, passed=True, diagnostic_only=True, performance_qualification=False, exit_code=code,
            case=args.case, measurement=measurement, cpu_gpu_diagnostics=publications,
            identity_before=before, identity_after=after,
            artifacts={name: file_identity(output / name) for name in ("runtime.log", "process_tail.txt", RAW_NAME, "cpu_gpu_summary.json")},
            interpretation="Same final renderer and resource contract; scopes overlap and are not summed. This is CPU/frame diagnostic evidence, not an FPS or speed qualification.")
        write_json(output / "result.json", result)
        return result
    finally:
        primary_failure = sys.exc_info()[0] is not None
        errors = []
        if process is not None:
            try:
                _, tail = terminate_process(process, "reflection CPU diagnostic")
                (output / "process_tail.txt").write_text(tail, encoding="utf-8")
            except Exception as error:
                errors.append(str(error))
        if not collected and log_directory is not None:
            try:
                text = shutdown_logserver_and_collect(logger, log_directory, baseline, pattern)
                (output / "runtime.log").write_text(text, encoding="utf-8")
                logger = None
            except Exception as error:
                errors.append(str(error))
        try:
            terminate_process(logger, "logserver")
            after = identities(args)
            write_json(output / "identity_after.json", after)
            if before != after:
                errors.append("guarded inputs changed during acquisition")
        except Exception as error:
            errors.append(str(error))
        if errors:
            write_json(output / "cleanup_error.json", dict(errors=errors))
            if not primary_failure:
                raise SmokeFailure("; ".join(errors))


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--case", choices=CASES, required=True)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise SmokeFailure("reflection diagnostic deadline must be finite and positive")
    for name in ("executable", "working_directory", "logserver_executable", "output_directory"):
        setattr(args, name, getattr(args, name).resolve())
    if not args.executable.is_file() or not args.logserver_executable.is_file() or not args.working_directory.is_dir():
        raise SmokeFailure("reflection diagnostic executable, logger or runtime is missing")
    args.no_logserver, args.log_port, args.software_vulkan = False, 0, "off"
    return args


def main(argv=None):
    args = None
    try:
        args = parse_args(argv)
        result = acquire(args)
        print(f"PASS reflection CPU/GPU diagnostic {args.case}: {result['measurement']['presentations']} presentations")
        return 0
    except (SmokeFailure, SmokeSkip, OSError, ValueError, subprocess.SubprocessError) as error:
        if args is not None and args.output_directory.is_dir():
            write_json(args.output_directory / "failure.json", dict(passed=False, error=str(error), diagnostic_only=True))
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
