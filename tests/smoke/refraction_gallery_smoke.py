#!/usr/bin/env python3
"""Capture actual complex-refraction cases and package an offline comparison gallery."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from window_capture_image import analyze_rgb_rows, read_bmp_24_rows
from refraction_gallery_publisher import LIT_GALLERY_HTML, RefractionGalleryPublisher
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 30

LIT_ENABLED_FLAG = "1"
LIT_DISABLED_FLAG = "0"

LIT_IMAGE_COUNT = "image_count"

LIT_SINGLE = "single"
LIT_GEOMETRY = "geometry"
LIT_AUTOMATIC = "automatic"
LIT_SCREEN = "screen"
LIT_DISABLED = "disabled"
LIT_NWB_REFRACTION_SMOKE_CASE = "NWB_REFRACTION_SMOKE_CASE"
LIT_NWB_REFRACTION_SMOKE_GEOMETRY = "NWB_REFRACTION_SMOKE_GEOMETRY"
LIT_NWB_REFRACTION_SMOKE_ENABLED = "NWB_REFRACTION_SMOKE_ENABLED"
LIT_NWB_REFRACTION_SMOKE_HARDWARE = "NWB_REFRACTION_SMOKE_HARDWARE"
LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO = "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"
LIT_CHANGED_PIXELS = "changed_pixels"
LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_TIMEOUT = "--timeout"
LIT_EXPECT_LOG_MESSAGE = "--expect-log-message"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_AVBOIT_REFRACTION_RESOLVE = "AVBOIT refraction resolve:"
LIT_UTF_8 = "utf-8"
LIT_LIMITS = "limits"
LIT_CASES = "cases"
LIT_CAPTURES = "captures"
LIT_FILE = "file"
LIT_COMMA_SEPARATED_SELECTION_DEFAULTS_TO_ = "Comma-separated selection; defaults to all: "
LIT_VARIANTS = "variants"
LIT_ID = "id"
LIT_MAIN = "__main__"
LIT_STORE_TRUE = "store_true"
LIT_APPEND = "append"


CASES = { "identity_reference": ("Exact entity reference", "A tinted retained CSG slab at z=0 through z=1 uses a recycled entity generation beyond float32 integer precision.",
        "The camera near plane lies just before its raw front face; the screen control establishes the captured primary without a competing receiver."),
    "identity_same_ior": ("Same-IOR neighbor", "A thin neighboring glass volume shares IOR 1.5 with the primary, but its entry is clipped from raster capture by the camera near plane.",
        "The retained slab front is nearer than the neighbor backface and owns capture; the clipped foreign entry remains inside the hardware association band and must fail exact entity association."),
    "identity_different_ior": ("Different-IOR control", "The same material and pipeline use typed mutable IOR 3.8 on the raster-clipped thin neighbor.",
        "This distinguishes identity association from the retired IOR-similarity heuristic; the selected primary and screen result remain the same."),
    "crossing_overflow": ("Diagnosed crossing overflow", "One narrow CSG receiver contains 33 disconnected closed boxes, producing 66 central crossings.",
        "Hardware must preserve the explicit capacity failure and return conservative black; screen refraction remains a separate transmitting approximation."),
    "near_air_ordinary": ("Near-air closed volume", "A three-unit tinted box at z=-0.25 through z=2.75 uses the first half-precision IOR above one (1.0009765625).",
        "Negligible bending still requires the measured Beer path; the transmitting screen control deliberately uses one-unit thickness."),
    "near_air_csg": ("Near-air CSG volume", "The same three-unit near-air box at z=-0.25 through z=2.75 has a plane cutter outside its retained silhouette.",
        "Current CSG context must preserve the ordinary volume's entry, exit and attenuation, including this valid IOR just above air."), "csg_reference": ("Retained slab reference", "An ordinary glass slab occupies z=0 through z=1.",
        "Its entry, exit and material match the retained solid in the CSG cap case."), "csg_cap": ("Generated glass entry", "A plane removes the front half of a glass box, leaving z=0 through z=1.",
        "The generated wall must retain the reference slab's refraction direction and physical absorption path."), "csg_middle": ("Generated entry and exit", "Two planes retain only the middle z=0 through z=1 of a larger glass box.",
        "Both visible-ray receiver triangles are removed; generated walls must still admit the retained glass primary."), "csg_uncut": ("Uncut thickness control", "The original glass box retains its full z=-1 through z=1 thickness.",
        "This control must differ from the carved slab so an original-triangle trace cannot pass the comparison."), LIT_SINGLE: ("Single solid", "A single closed glass volume in front of the stripe wall.",
        "The baseline supported case: one primary entry and exit. Compare the bent background with the straight red foreground panel."), "separate": ("Separate objects", "Distinct glass objects with separate visible silhouettes.",
        "Each pixel can retain its own nearest refractor. Spatially separate objects can therefore use different primary interfaces."), "stacked": ("Depth stack", "Multiple glass objects placed one behind another along the view.",
        "After a valid first exit, further glass uses an approximation from that exit while preserving the completed primary direction, measured thickness, absorption and Fresnel. No extra rays trace the remaining volume chain."),
    "intersecting": ("Intersecting volumes", "Glass objects whose solid volumes overlap.", "Overlapping volumes do not have a tracked medium stack. When another instance interrupts the selected volume's exit path, the hardware resolver falls back per pixel."),
    "nested": ("Nested volumes", "One glass object contained inside another.", "Nested media are outside the first implementation's complete optical model. Encountering the inner instance can trigger the screen-space fallback."),
    "coincident": ("Coincident surfaces", "Exact coincident glass with different tints and an explicit shared optical-volume group.",
        "The cool instance has priority 10 and the warm instance priority 0. The renderer should select one cool optical volume; the geometry preview still shows both authored surfaces."),
    "coincident_tinted": ("Coincident tinted glass", "Coincident glass instances with different authored optical tints.",
        "Both instances have coverage 0.3 and share an authored optical-volume group. Cool priority 10 wins over warm priority 0, avoiding duplicate optical attenuation."),
    "torus": ("Concave torus", "A closed ring with a visible hole and a concave inner surface.",
        "A valid entry/exit pair handles one local ring segment. If the bent ray meets the ring again, the remaining background is approximated from the completed exit without discarding its optical result or adding rays. Invalid exit sequences still use the conservative entry fallback."),
    "same_mesh": ("Disconnected components", "Multiple disconnected glass components inside one mesh instance.",
        "Primary identity is an instance, not a connected-volume label, so exclusion can affect its other components. After a valid first exit, later components use the exit-based background approximation; a full optical chain through disconnected components is not traced."),
    "prism": ("Faceted prism", "A glass prism with planar faces and sharp changes in surface normal.",
        "The selected entry and exit faces bend the ray using their normals. Internal reflection is capped, so sharp-angle regions can lose unresolved reflected energy. This angle-stress scene does not verify that the total-internal-reflection branch executed."),
    "duplicate_single_cool": ("Cool reference", "One cool glass sphere of radius 1.1, matching the duplicate fixtures exactly.", "Zero coverage; its actual render is the reference for cool-priority and identical-material clear duplicate cases."),
    "duplicate_single_warm": ("Warm reference", "One warm glass sphere of radius 1.1, matching the duplicate fixtures exactly.", "Zero coverage; its actual render is the reference when explicit optical priority selects the warm instance."),
    "duplicate_single_cool_tinted": ("Cool tinted reference", "One cool glass sphere of radius 1.1 with coverage 0.3.", "Reference for the cool-priority and identical-material duplicate cases with nonzero coverage."),
    "duplicate_single_warm_tinted": ("Warm tinted reference", "One warm glass sphere of radius 1.1 with coverage 0.3.", "Reference when explicit optical priority selects the warm instance with nonzero coverage."),
    "coincident_reversed": ("Reversed creation order", "The warm instance is created before the cool instance at exactly the same transform.", "The cool instance still has priority 10. Reversing creation order should preserve its single-volume result."),
    "coincident_tinted_reversed": ("Reversed tinted order", "The coverage-0.3 pair is created warm first, cool second.", "Explicit cool priority should preserve the cool single reference despite the reversed insertion order."),
    "coincident_priority_swap": ("Swapped priority", "The same clear duplicate pair now gives the warm instance priority 10.", "The result should match the warm single reference; optical priority changes the selected material."),
    "coincident_tinted_priority_swap": ("Swapped tinted priority", "The coverage-0.3 duplicate pair now gives the warm instance priority 10.", "The result should match the warm tinted single reference without adding the lower-priority surface."),
    "coincident_identical": ("Identical-material duplicate", "Two identical clear spheres explicitly opt into IdenticalMaterial coincidence handling.",
        "The author guarantees instance-independent geometry. Exact static geometry, transform, material and effective inputs should collapse to the cool single reference."),
    "coincident_tinted_identical": ("Identical tinted duplicate", "Two identical coverage-0.3 spheres explicitly opt into IdenticalMaterial coincidence handling.",
        "The opted-in duplicate should not add an extra optical or AVBOIT layer, including when camera refraction is disabled."),
    "near_coincident": ("Near overlap retained", "The warm member of an authored group is translated 0.04 units from the cool sphere.",
        "The transforms are different, so the renderer must retain both volumes. The existing bounded overlap fallback still applies."),
    "coincident_preserved": ("Independent duplicate layers", "Two identical tinted spheres retain the default Independent coincidence policy.",
        "Independent surfaces remain separate. Their layered result remains subject to the renderer's bounded interface model."), }

VARIANTS = (LIT_GEOMETRY, LIT_AUTOMATIC, LIT_SCREEN, LIT_DISABLED)
VARIANT_LABELS = { LIT_GEOMETRY: "Geometry preview — translucent, refraction off", LIT_AUTOMATIC: "Automatic tracing", LIT_SCREEN: "Screen-space fallback", LIT_DISABLED: "Refraction disabled", }
COMMON_LIMIT = ("Current implementation: one primary refractor per pixel, bounded hardware continuation, " "and opted-in exact static duplicate grouping. Additional glass after a valid first exit uses "
    "an exit-based background approximation that preserves the primary direction, thickness, absorption " "and Fresnel without extra rays. Invalid first exits, intersections and nested media retain the "
    "conservative entry fallback. A hardware dispatch can still fall back on individual pixels. " "These are real renderer outputs; the gallery measures " "visible changes and exposes limitations without certifying complex optical accuracy.")


def parse_selection(text, allowed, option):
    selected = tuple(part.strip() for part in text.split(",") if part.strip())
    if not selected:
        raise argparse.ArgumentTypeError(f"{option} must select at least one item")
    invalid = [item for item in selected if item not in allowed]
    if invalid:
        raise argparse.ArgumentTypeError(f"unknown {option}: {', '.join(invalid)}; choices: {', '.join(allowed)}")
    if len(set(selected)) != len(selected):
        raise argparse.ArgumentTypeError(f"duplicate {option} selections are not allowed")
    return selected


def capture_environment(case, variant, inherited=None):
    env = dict(os.environ if inherited is None else inherited)
    for name in ("NWB_REFRACTION_SMOKE_LIFECYCLE", "NWB_REFRACTION_SMOKE_EXTENT", "NWB_REFRACTION_SMOKE_TIMING", "NWB_REFRACTION_SMOKE_DENSITY", "NWB_REFRACTION_FRAME_GRAPH_SNAPSHOT_PATH"):
        env.pop(name, None)
    for name in (LIT_NWB_REFRACTION_SMOKE_CASE, LIT_NWB_REFRACTION_SMOKE_GEOMETRY, LIT_NWB_REFRACTION_SMOKE_ENABLED, LIT_NWB_REFRACTION_SMOKE_HARDWARE, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO,
        "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",):
        env.pop(name, None)
    env[LIT_NWB_REFRACTION_SMOKE_CASE] = case
    env[LIT_NWB_REFRACTION_SMOKE_GEOMETRY] = LIT_ENABLED_FLAG if variant == LIT_GEOMETRY else LIT_DISABLED_FLAG
    env[LIT_NWB_REFRACTION_SMOKE_ENABLED] = LIT_DISABLED_FLAG if variant in (LIT_GEOMETRY, LIT_DISABLED) else LIT_ENABLED_FLAG
    env[LIT_NWB_REFRACTION_SMOKE_HARDWARE] = LIT_DISABLED_FLAG if variant == LIT_SCREEN else LIT_ENABLED_FLAG
    env[LIT_NWB_RENDERER_BASELINE_FIXED_DELTA_SECO] = "0.016666667"
    return env


def frame_difference(reference, output):
    if reference[:2] != output[:2]:
        raise SmokeFailure("gallery captures have different framebuffer dimensions")
    width, height, reference_rows = reference
    changed = 0
    channel_sum = 0
    maximum = 0
    for before_row, after_row in zip(reference_rows, output[2]):
        for before, after in zip(before_row, after_row):
            deltas = tuple(abs(a - b) for a, b in zip(before, after))
            largest = max(deltas)
            changed += largest > 8
            channel_sum += sum(deltas)
            maximum = max(maximum, largest)
    pixels = width * height
    return { "compared_with": LIT_DISABLED, LIT_CHANGED_PIXELS: changed, "total_pixels": pixels, "changed_fraction": changed / pixels, "mean_absolute_rgb_difference": channel_sum / (pixels * 3), "maximum_channel_difference": maximum, }


def validate_frame(frame, case, variant):
    width, height, rows = frame
    if (width, height) != (960, 720):
        raise SmokeFailure(f"{case}/{variant}: expected 960x720 framebuffer, got {width}x{height}")
    analysis = analyze_rgb_rows(rows)
    if not analysis.has_pixel_variation or analysis.appears_empty_or_white:
        raise SmokeFailure(f"{case}/{variant}: framebuffer is empty or lacks scene variation")




def stage_logserver(executable, output_directory):
    executable = executable.resolve()
    runtime = Path(tempfile.mkdtemp(prefix="logserver_", dir=output_directory))
    staged = runtime / executable.name
    try:
        shutil.copy2(executable, staged)
        symbols = executable.with_suffix(".pdb")
        try:
            symbols.stat()
        except FileNotFoundError:
            pass
        else:
            shutil.copy2(symbols, runtime / symbols.name)
        for symbols in executable.parent.glob("*.namesym"):
            if symbols.is_file():
                shutil.copy2(symbols, runtime / symbols.name)
    except OSError as error:
        raise SmokeFailure(f"could not stage logserver; files remain in '{runtime}': {error}") from error
    return staged, executable.parent


def capture(args, case, variant):
    output = args.output_directory / f"{case}_{variant}.bmp"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--log-output", str(output.with_suffix(".log")), "--application-capture", "--application-capture-frame-count", str(args.frames), LIT_TIMEOUT, str(args.timeout),
        LIT_EXPECT_LOG_MESSAGE, f"RefractionSmokeProject: gallery case {case} created", LIT_EXPECT_LOG_MESSAGE, "RefractionSmokeProject: shutdown", LIT_EXPECT_LOG_MESSAGE, "RefractionSmokeProject: refraction " + (
            LIT_DISABLED if variant in (LIT_GEOMETRY, LIT_DISABLED) else "enabled"),]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if variant == LIT_AUTOMATIC:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve: hardware" if args.require_hardware else LIT_AVBOIT_REFRACTION_RESOLVE]
    elif variant == LIT_SCREEN:
        command += [LIT_EXPECT_LOG_MESSAGE, "AVBOIT refraction resolve: screen-space"]
    else:
        command += ["--reject-log-message", LIT_AVBOIT_REFRACTION_RESOLVE]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {case}/{variant} ({args.frames} frames)...", flush=True)
    environment = capture_environment(case, variant)
    library_directory = getattr(args, "logserver_library_directory", None)
    if library_directory:
        for variable in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
            previous = environment.get(variable, "")
            environment[variable] = str(library_directory) + (os.pathsep + previous if previous else "")
    completed = subprocess.run(command, env=environment, check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
    if completed.returncode == SKIP_EXIT_CODE:
        return None
    if completed.returncode != 0:
        raise SmokeFailure(f"{case}/{variant}: capture failed with exit {completed.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame, case, variant)
    return frame






def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument("--cases", type=lambda value: parse_selection(value, CASES, LIT_CASES), default=tuple(CASES), help=LIT_COMMA_SEPARATED_SELECTION_DEFAULTS_TO_ + ", ".join(CASES))
    parser.add_argument("--variants", type=lambda value: parse_selection(value, VARIANTS, LIT_VARIANTS), default=VARIANTS, help=LIT_COMMA_SEPARATED_SELECTION_DEFAULTS_TO_ + ", ".join(VARIANTS))
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument(LIT_TIMEOUT, type=float, default=60.0, help="Capture timeout in seconds for each child run.")
    parser.add_argument("--require-hardware", action=LIT_STORE_TRUE)
    parser.add_argument("--application-arg", action=LIT_APPEND, default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    labels = dict(VARIANT_LABELS)
    if args.require_hardware:
        labels[LIT_AUTOMATIC] = "Hardware RT (per-pixel fallback allowed)"
    manifest = { "captured_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"), "frames": args.frames, "hardware_required": args.require_hardware, "application_args": list(args.application_arg),
        LIT_VARIANTS: list(args.variants), "variant_labels": labels, LIT_LIMITS: COMMON_LIMIT, LIT_IMAGE_COUNT: len(args.cases) * len(args.variants), LIT_CASES: [],
        "validation": "Strict runtime logs, successful route dispatch, normal shutdown, nontrivial frames, and visible changes when a disabled baseline is present.", }
    try:
        args.logserver_library_directory = None
        if args.logserver_executable:
            args.logserver_executable, args.logserver_library_directory = stage_logserver(args.logserver_executable, args.output_directory)
        for case_id in args.cases:
            title, description, note = CASES[case_id]
            entry = {LIT_ID: case_id, "title": title, "description": description, "note": note, LIT_CAPTURES: []}
            frames = {}
            for variant in args.variants:
                frame = capture(args, case_id, variant)
                if frame is None:
                    print(f"SKIP: framebuffer capture unavailable for {case_id}/{variant}", file=sys.stderr)
                    return SKIP_EXIT_CODE
                frames[variant] = frame
                entry[LIT_CAPTURES].append({LIT_ID: variant, LIT_FILE: f"{case_id}_{variant}.bmp"})
            if LIT_DISABLED in frames:
                for captured in entry[LIT_CAPTURES]:
                    variant = captured[LIT_ID]
                    if variant in (LIT_DISABLED, LIT_GEOMETRY):
                        continue
                    metrics = frame_difference(frames[LIT_DISABLED], frames[variant])
                    captured["difference"] = metrics
                    if metrics[LIT_CHANGED_PIXELS] < 64:
                        raise SmokeFailure(f"{case_id}/{variant}: fewer than 64 pixels changed from disabled baseline; expected a visible scene effect")
            manifest[LIT_CASES].append(entry)
            del frames
        RefractionGalleryPublisher.write(args.output_directory, manifest)
        print(f"PASS: {manifest[LIT_IMAGE_COUNT]} actual captures; offline gallery: {args.output_directory / LIT_GALLERY_HTML}", flush=True)
        return 0
    except (OSError, SmokeFailure, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == LIT_MAIN:
    raise SystemExit(main(sys.argv[1:]))
