"""Validate smoke-owned CPU/GPU publications without treating overlapping scopes as wall-time components."""

import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ab"))
from name_symbols import known_name_symbols

from window_capture_smoke import SmokeFailure

LIT_GRAPHICS_FRAME = "graphics.frame"
LIT_GRAPHICS_PREPARE_RESOURCES = "graphics.prepare_resources"
LIT_GRAPHICS_RENDER_PASSES = "graphics.render_passes"
LIT_GRAPHICS_BEGIN_FRAME = "graphics.begin_frame"
LIT_GRAPHICS_PRESENT = "graphics.present"
LIT_WINDOW = "window"
LIT_SECONDS = "seconds"
LIT_NAME = "name"
LIT_GPU = "gpu"
LIT_CPU = "cpu"
LIT_DOMAIN = "domain"
LIT_SAMPLES = "samples"
LIT_MINIMUM_SAMPLE_SECONDS = "minimum_sample_seconds"
LIT_MAXIMUM_SAMPLE_SECONDS = "maximum_sample_seconds"
LIT_FIRST_SOURCE_FRAME = "first_source_frame"
LIT_LAST_SOURCE_FRAME = "last_source_frame"
LIT_SINGLE_SOURCE_FRAMES = "single_source_frames"
LIT_UTF_8 = "utf-8"

ENABLED = "SmokeCpuGpuTimingProbe: enabled "
COMPLETE = "SmokeCpuGpuTimingProbe: complete "
REQUIRED_CPU = {LIT_GRAPHICS_FRAME, LIT_GRAPHICS_PREPARE_RESOURCES, LIT_GRAPHICS_RENDER_PASSES, LIT_GRAPHICS_BEGIN_FRAME, LIT_GRAPHICS_PRESENT}
# Exact stable producer scope strings; the helper computes global/name.h's canonical eight-lane hash.
# This is not an externally supplied dictionary. Unrecognized labels retain their original token.
KNOWN_CPU = ("frame.project_update", LIT_GRAPHICS_FRAME, "graphics.animate", LIT_GRAPHICS_BEGIN_FRAME,
    "graphics.frame_preamble", "graphics.render", LIT_GRAPHICS_PREPARE_RESOURCES, LIT_GRAPHICS_RENDER_PASSES,
    "graphics.prepare_resources_failed", LIT_GRAPHICS_PRESENT, "graphics.garbage_collect",
    "cpu.task.queue_delay", "cpu.task.execution", "cpu.task.handle_join", "cpu.task.scope_join",
    "cpu.task.scheduler_join", "cpu.worker.idle", "cpu.task.ecs.world", "cpu.task.graphics.setup", "cpu.task.graphics.frame")
KNOWN_GPU = ("render.frame", "render.async_prefix", "render.async_shadow", "render.async_surfel_gi", "render.async_final",
    "render.mesh_dispatch", "render.opaque_regular", "render.shadow_visibility", "render.caustic_photons",
    "render.caustic_resolve", "render.deferred_lighting", "render.deferred_composite", "render.deferred_present",
    "render.reflection_classify", "render.reflection_hardware", "render.reflection_spatial",
    "render.shadow.light_space_views", "render.shadow.light_space_opaque_capture",
    "render.shadow.light_space_transparent_capture", "render.shadow.light_space_shade",
    "render.shadow.light_space_map_opaque", "render.shadow.light_space_map_transparent",
    "render.shadow.light_space_fallback_opaque", "render.shadow.light_space_fallback_transparent",
    "render.surfel_spawn", "render.surfel_age_free", "render.surfel_hash_build", "render.surfel_trace",
    "render.surfel_resolve", "render.surfel_upsample")
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
    if len(lines) < 5 or lines[:2] != ["NWB_SMOKE_CPU_GPU_DIAGNOSTIC 1", "capture cpu=1 gpu=1 memory=0 diagnostic_only=1"]:
        raise SmokeFailure("CPU diagnostic schema or capture options mismatch")
    window = lines[2].split()
    if len(window) != 6 or window[0] != LIT_WINDOW:
        raise SmokeFailure("CPU diagnostic measurement window missing")
    first, last, source_first, source_end = map(_integer, window[1:5])
    seconds = _seconds(window[5])
    if (first, last) != (measurement["first"], measurement["last"]) or last <= first or source_end <= source_first:
        raise SmokeFailure("CPU diagnostic presentation/source boundaries disagree")
    if not math.isclose(seconds, measurement[LIT_SECONDS], rel_tol=1e-7, abs_tol=1e-6):
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
            parts = line.split(" ", 4)
            if seen_samples or len(parts) != 5:
                raise SmokeFailure("CPU diagnostic scope declaration misplaced")
            domain, index, generation = map(_integer, parts[1:4])
            raw_name = parts[4]
            name = CHECKED_SYMBOLS.get(raw_name, raw_name)
            if domain not in (0, 1) or index >= 512 or generation == 0 or not name or any(ord(char) < 32 for char in name):
                raise SmokeFailure("CPU diagnostic scope identity invalid")
            key = (domain, index)
            if key in scopes or any(row[LIT_NAME] == name and prior[0] == domain for prior, row in scopes.items()):
                raise SmokeFailure("CPU diagnostic duplicate scope identity")
            scopes[key] = dict(name=name, generation=generation, raw_name=raw_name, decoded_from_hash=name != raw_name, domain=LIT_GPU if domain else LIT_CPU, records=0, samples=0, seconds=0.0,
                minimum_sample_seconds=None, maximum_sample_seconds=0.0, first_source_frame=None,
                last_source_frame=None, single_source_frames=set(), excluded_records=0, excluded_samples=0)
            continue
        parts = line.split()
        if len(parts) != 14 or parts[0] != "sample":
            raise SmokeFailure("CPU diagnostic sample record malformed")
        seen_samples = True
        domain, index, generation, observation, presentations, publication, begin, end, count = map(_integer, parts[1:10])
        total, minimum, maximum, latest = map(_seconds, parts[10:14])
        key = (domain, index)
        if key not in scopes or generation != scopes[key]["generation"] or count == 0 or begin > end or end > observation or publication > observation:
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
        records.append(dict(domain=row[LIT_DOMAIN], scope=row[LIT_NAME], generation=generation, observation_frame=observation,
            presentations=presentations, publication_frame=publication, first_source_frame=begin,
            last_source_frame=end, samples=count, seconds=total, included=included))
        if not included:
            row["excluded_records"] += 1
            row["excluded_samples"] += count
            continue
        row["records"] += 1
        row[LIT_SAMPLES] += count
        row[LIT_SECONDS] += total
        row[LIT_MINIMUM_SAMPLE_SECONDS] = minimum if row[LIT_MINIMUM_SAMPLE_SECONDS] is None else min(row[LIT_MINIMUM_SAMPLE_SECONDS], minimum)
        row[LIT_MAXIMUM_SAMPLE_SECONDS] = max(row[LIT_MAXIMUM_SAMPLE_SECONDS], maximum)
        row[LIT_FIRST_SOURCE_FRAME] = begin if row[LIT_FIRST_SOURCE_FRAME] is None else min(row[LIT_FIRST_SOURCE_FRAME], begin)
        row[LIT_LAST_SOURCE_FRAME] = end if row[LIT_LAST_SOURCE_FRAME] is None else max(row[LIT_LAST_SOURCE_FRAME], end)
        if begin == end:
            row[LIT_SINGLE_SOURCE_FRAMES].add(begin)
    if (len(records), len(scopes)) != (expected_records, expected_scopes) or len(records) > 262144:
        raise SmokeFailure("CPU diagnostic completion counts mismatch")
    present_cpu = {row[LIT_NAME] for row in scopes.values() if row[LIT_DOMAIN] == LIT_CPU and row[LIT_SAMPLES]}
    if not REQUIRED_CPU.issubset(present_cpu):
        raise SmokeFailure("CPU diagnostic required runtime phases are missing")
    if not any(row[LIT_DOMAIN] == LIT_GPU and row[LIT_SAMPLES] for row in scopes.values()):
        raise SmokeFailure("CPU diagnostic requires captured GPU publications too")
    result_scopes = []
    for row in scopes.values():
        row["single_source_frame_count"] = len(row.pop(LIT_SINGLE_SOURCE_FRAMES))
        row["sample_mean_ms"] = 1000 * row[LIT_SECONDS] / row[LIT_SAMPLES] if row[LIT_SAMPLES] else None
        row["total_ms_per_accepted_presentation"] = 1000 * row[LIT_SECONDS] / (last - first)
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


def verify_capture(log_text, path, measurement, requested, owner):
    lines = [line.strip() for line in log_text.splitlines()]
    enabled = [line for line in lines if line.startswith("SmokeCpuGpuTimingProbe: enabled")]
    completed = [line for line in lines if line.startswith(COMPLETE.rstrip())]
    if not requested:
        if enabled or completed or path.exists():
            raise SmokeFailure("CPU profiling was recorded without an explicit diagnostic request")
        return dict(requested=False, diagnostic_only=False, performance_qualification=True)
    expected_enabled = ENABLED + f"owner={owner} cpu=1 gpu=1 memory=0 diagnostic_only=1"
    if enabled != [expected_enabled] or len(completed) != 1 or not path.is_file():
        raise SmokeFailure("CPU diagnostic producer or completed output is missing")
    result = parse_publications(path.read_text(encoding=LIT_UTF_8), measurement)
    match = re.fullmatch(re.escape(COMPLETE + f"owner={owner} ") + r"records=([0-9]+) first=([0-9]+) last=([0-9]+) first_source=([0-9]+) end_source=([0-9]+)", completed[0])
    window = result[LIT_WINDOW]
    expected = (len(result["publications"]), window["first_presentation"], window["last_presentation"],
        window[LIT_FIRST_SOURCE_FRAME], window["end_source_frame_exclusive"])
    if match is None or tuple(map(int, match.groups())) != expected:
        raise SmokeFailure("CPU diagnostic completion marker disagrees with output")
    return result
