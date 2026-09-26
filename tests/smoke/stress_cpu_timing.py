"""Validate smoke-owned CPU/GPU publications without treating overlapping scopes as wall-time components."""

import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from name_symbols import known_name_symbols

from window_capture_smoke import SmokeFailure

ENABLED = "StressCpuTimingProbe: enabled cpu=1 gpu=1 memory=0 diagnostic_only=1"
COMPLETE = "StressCpuTimingProbe: complete "
REQUIRED_CPU = {"graphics.frame", "graphics.prepare_resources", "graphics.render_passes", "graphics.begin_frame", "graphics.present"}
# Exact stable producer scope strings; the helper computes global/name.h's canonical eight-lane hash.
# This is not an externally supplied dictionary. Unrecognized labels retain their original token.
KNOWN_CPU = ("frame.project_update", "graphics.frame", "graphics.animate", "graphics.begin_frame",
    "graphics.frame_preamble", "graphics.render", "graphics.prepare_resources", "graphics.render_passes",
    "graphics.prepare_resources_failed", "graphics.present", "graphics.garbage_collect",
    "cpu.task.queue_delay", "cpu.task.execution", "cpu.task.handle_join", "cpu.task.scope_join",
    "cpu.task.scheduler_join", "cpu.worker.idle", "cpu.task.ecs.world", "cpu.task.graphics.setup", "cpu.task.graphics.frame")
KNOWN_GPU = ("render.frame", "render.async_prefix", "render.async_shadow", "render.async_surfel_gi", "render.async_final",
    "render.mesh_dispatch", "render.opaque_regular", "render.shadow_visibility", "render.caustic_photons",
    "render.caustic_resolve", "render.deferred_lighting", "render.deferred_composite", "render.deferred_present",
    "render.reflection_classify", "render.reflection_hardware", "render.reflection_spatial",
    "render.shadow.light_space_views", "render.shadow.light_space_opaque_capture",
    "render.shadow.light_space_transparent_capture", "render.shadow.light_space_shade",
    "render.shadow.light_space_map_opaque", "render.shadow.light_space_map_transparent",
    "render.shadow.light_space_fallback_opaque", "render.shadow.light_space_fallback_transparent")
CHECKED_SYMBOLS = known_name_symbols(KNOWN_CPU + KNOWN_GPU)



def _integer(value):
    if not re.fullmatch(r"[0-9]+", value):
        raise SmokeFailure("CPU diagnostic integer is malformed")
    return int(value)


def _seconds(value):
    try:
        result = float(value)
    except ValueError as error:
        raise SmokeFailure("CPU diagnostic duration is malformed") from error
    if not math.isfinite(result) or result < 0:
        raise SmokeFailure("CPU diagnostic duration must be finite and nonnegative")
    return result


def parse_publications(text, measurement):
    lines = text.splitlines()
    if len(lines) < 5 or lines[:2] != ["NWB_STRESS_CPU_GPU_DIAGNOSTIC 1", "capture cpu=1 gpu=1 memory=0 diagnostic_only=1"]:
        raise SmokeFailure("CPU diagnostic schema or capture options mismatch")
    window = lines[2].split()
    if len(window) != 6 or window[0] != "window":
        raise SmokeFailure("CPU diagnostic measurement window missing")
    first, last, source_first, source_end = map(_integer, window[1:5])
    seconds = _seconds(window[5])
    if (first, last) != (measurement["first"], measurement["last"]) or last <= first or source_end <= source_first:
        raise SmokeFailure("CPU diagnostic presentation/source boundaries disagree")
    if not math.isclose(seconds, measurement["seconds"], rel_tol=1e-7, abs_tol=1e-6):
        raise SmokeFailure("CPU diagnostic steady-clock window differs from presentation measurement")
    completion = lines[-1].split()
    if len(completion) != 3 or completion[0] != "complete":
        raise SmokeFailure("CPU diagnostic output is incomplete")
    expected_records, expected_scopes = map(_integer, completion[1:])
    scopes, records = {}, []
    last_publications = {}
    last_observation = (source_first, first)
    seen_samples = False
    for line in lines[3:-1]:
        if line.startswith("scope "):
            parts = line.split(" ", 3)
            if seen_samples or len(parts) != 4:
                raise SmokeFailure("CPU diagnostic scope declaration misplaced")
            domain, index = map(_integer, parts[1:3])
            raw_name = parts[3]
            name = CHECKED_SYMBOLS.get(raw_name, raw_name)
            if domain not in (0, 1) or index >= 512 or not name or any(ord(char) < 32 for char in name):
                raise SmokeFailure("CPU diagnostic scope identity invalid")
            key = (domain, index)
            if key in scopes or any(row["name"] == name and prior[0] == domain for prior, row in scopes.items()):
                raise SmokeFailure("CPU diagnostic duplicate scope identity")
            scopes[key] = dict(name=name, raw_name=raw_name, decoded_from_hash=name != raw_name, domain="gpu" if domain else "cpu", records=0, samples=0, seconds=0.0,
                minimum_sample_seconds=None, maximum_sample_seconds=0.0, first_source_frame=None,
                last_source_frame=None, single_source_frames=set(), excluded_records=0, excluded_samples=0)
            continue
        parts = line.split()
        if len(parts) != 13 or parts[0] != "sample":
            raise SmokeFailure("CPU diagnostic sample record malformed")
        seen_samples = True
        domain, index, observation, presentations, publication, begin, end, count = map(_integer, parts[1:9])
        total, minimum, maximum, latest = map(_seconds, parts[9:13])
        key = (domain, index)
        if key not in scopes or count == 0 or begin > end or end > observation or publication > observation:
            raise SmokeFailure("CPU diagnostic sample identity or source bounds invalid")
        if observation < last_observation[0] or presentations < last_observation[1]:
            raise SmokeFailure("CPU diagnostic observations regressed")
        if not source_first <= observation <= source_end or not first <= presentations <= last:
            raise SmokeFailure("CPU diagnostic observation lies outside measurement")
        if key in last_publications and publication <= last_publications[key]:
            raise SmokeFailure("CPU diagnostic publication repeated or regressed")
        tolerance = max(1e-12, total * 1e-9)
        if minimum > maximum or not minimum - tolerance <= latest <= maximum + tolerance:
            raise SmokeFailure("CPU diagnostic per-sample range invalid")
        if total < minimum * count - tolerance or total > maximum * count + tolerance:
            raise SmokeFailure("CPU diagnostic total inconsistent with sample range")
        last_publications[key] = publication
        last_observation = (observation, presentations)
        row = scopes[key]
        included = begin >= source_first and end < source_end
        records.append(dict(domain=row["domain"], scope=row["name"], observation_frame=observation,
            presentations=presentations, publication_frame=publication, first_source_frame=begin,
            last_source_frame=end, samples=count, seconds=total, included=included))
        if not included:
            row["excluded_records"] += 1
            row["excluded_samples"] += count
            continue
        row["records"] += 1
        row["samples"] += count
        row["seconds"] += total
        row["minimum_sample_seconds"] = minimum if row["minimum_sample_seconds"] is None else min(row["minimum_sample_seconds"], minimum)
        row["maximum_sample_seconds"] = max(row["maximum_sample_seconds"], maximum)
        row["first_source_frame"] = begin if row["first_source_frame"] is None else min(row["first_source_frame"], begin)
        row["last_source_frame"] = end if row["last_source_frame"] is None else max(row["last_source_frame"], end)
        if begin == end:
            row["single_source_frames"].add(begin)
    if (len(records), len(scopes)) != (expected_records, expected_scopes) or len(records) > 262144:
        raise SmokeFailure("CPU diagnostic completion counts mismatch")
    present_cpu = {row["name"] for row in scopes.values() if row["domain"] == "cpu" and row["samples"]}
    if not REQUIRED_CPU.issubset(present_cpu):
        raise SmokeFailure("CPU diagnostic required runtime phases are missing")
    if not any(row["domain"] == "gpu" and row["samples"] for row in scopes.values()):
        raise SmokeFailure("CPU diagnostic requires captured GPU publications too")
    result_scopes = []
    for row in scopes.values():
        row["single_source_frame_count"] = len(row.pop("single_source_frames"))
        row["sample_mean_ms"] = 1000 * row["seconds"] / row["samples"] if row["samples"] else None
        row["total_ms_per_accepted_presentation"] = 1000 * row["seconds"] / (last - first)
        result_scopes.append(row)
    return dict(schema=1, requested=True, diagnostic_only=True, performance_qualification=False,
        capture=dict(cpu=True, gpu=True, memory=False),
        scope_decoding=dict(method="deterministic_canonical_engine_hash", decoder="tests/ab/name_symbols.py",
            known_cpu=list(KNOWN_CPU), known_gpu=list(KNOWN_GPU), unknown_labels="retained_raw",
            provenance="Decoder and this scope roster are bound by launch identity_before.helpers and identity_after.helpers."),
        window=dict(first_presentation=first, last_presentation=last,
            seconds=seconds, first_source_frame=source_first, end_source_frame_exclusive=source_end),
        scopes=result_scopes, publications=records,
        interpretation="Scopes overlap and task durations may overlap across workers; do not sum them or subtract GPU means from wall time.",
        gpu_coverage="Only completed publications observed by the final timestamp are included; no GPU drain or missing-sample imputation.")


def verify_capture(log_text, path, measurement, requested):
    lines = [line.strip() for line in log_text.splitlines()]
    enabled = [line for line in lines if line.startswith("StressCpuTimingProbe: enabled ")]
    completed = [line for line in lines if line.startswith(COMPLETE)]
    if not requested:
        if enabled or completed or path.exists():
            raise SmokeFailure("CPU profiling was recorded without an explicit diagnostic request")
        return dict(requested=False, diagnostic_only=False, performance_qualification=True)
    if enabled != [ENABLED] or len(completed) != 1 or not path.is_file():
        raise SmokeFailure("CPU diagnostic producer or completed output is missing")
    result = parse_publications(path.read_text(encoding="utf-8"), measurement)
    match = re.fullmatch(re.escape(COMPLETE) + r"records=([0-9]+) first=([0-9]+) last=([0-9]+) first_source=([0-9]+) end_source=([0-9]+)", completed[0])
    window = result["window"]
    expected = (len(result["publications"]), window["first_presentation"], window["last_presentation"],
        window["first_source_frame"], window["end_source_frame_exclusive"])
    if match is None or tuple(map(int, match.groups())) != expected:
        raise SmokeFailure("CPU diagnostic completion marker disagrees with output")
    return result
