#!/usr/bin/env python3
"""Renderer A/B caustic measurement analysis."""


import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import struct
from types import SimpleNamespace
from unittest.mock import patch
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "smoke"))
import renderer_ab_benchmark as benchmark
from renderer_ab_benchmark_test_support import scopes


LIT_A = "a"
LIT_B = "b"
LIT_NATURAL_HARDWARE_SHADOW_ROUTE = "natural hardware shadow route"
LIT_NATURAL_HYBRID_SHADOW_ROUTE = "natural hybrid shadow route"
LIT_B_ORIGINAL_FAILURE_EVIDENCE = b"original failure evidence"
LIT_TOTAL_MS = "total_ms"
LIT_GPU_SAMPLES = "gpu_samples"
LIT_REPORTS = "reports"
LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA = "RendererSystem: dispatched hardware transparent shadow traversal"
LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA = "RendererSystem: dispatched software shadow traversal"
LIT_MODIFIED = "modified"
LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32 = "AvboitTimingProbe: in-flight ranges 32"
LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1 = "AvboitTimingProbe: render unfocused 1"
LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG = "RendererSystem: deferred rendering targets ready (1280x900, samples=1)"
LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU = "GraphicsRuntime: created device 'Example GPU'"
LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED = "RendererSystem: material 'glass' selected CS + PS through compute emulation"
LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN = "TransparentMultiSmokeProject: shutdown"
LIT_SOURCE = "source"
LIT_REVISION = "revision"
LIT_UTF_8 = "utf-8"
LIT_SOURCE_JSON = "source.json"
LIT_FILES = "files"
LIT_RUNTIME = "runtime"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_ARM = "arm"
LIT_SAMPLE_RATIO = "sample ratio"
LIT_N_1280X900 = "1280x900"
LIT_BASELINE_EXECUTABLE = "--baseline-executable"
LIT_BASELINE_RUNTIME = "--baseline-runtime"
LIT_AR = "ar"
LIT_BASELINE_SOURCE_MANIFEST = "--baseline-source-manifest"
LIT_AS_JSON = "as.json"
LIT_CANDIDATE_EXECUTABLE = "--candidate-executable"
LIT_CANDIDATE_RUNTIME = "--candidate-runtime"
LIT_BR = "br"
LIT_CANDIDATE_SOURCE_MANIFEST = "--candidate-source-manifest"
LIT_BS_JSON = "bs.json"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_LOGGER = "logger"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_OUTPUT = "output"
LIT_CAUSTIC_POPULATED = "caustic-populated"
LIT_N_16 = "16"
LIT_WORKLOAD = "--workload"
LIT_POPULATED = "populated"
LIT_DISABLED = "disabled"
LIT_CAUSTIC = "caustic-"
LIT_POSITIVE_PIXELS = "positive_pixels"
LIT_POSITIVE_CHANNEL_GAIN = "positive_channel_gain"
LIT_POSITIVE_TILES = "positive_tiles"
LIT_SPARSE = "sparse"
LIT_QUALIFICATION_JSON = "qualification.json"
LIT_FROZEN = "frozen"
LIT_SCHEMA = "schema"
LIT_POLICY = "policy"
LIT_CAPTURES = "captures"
LIT_FAILURE_JSON = "failure.json"
LIT_FIXTURE_EXE = "fixture.exe"
LIT_LOGGER_EXE = "logger.exe"
LIT_GPUDBG = "--gpudbg"
LIT_WINDOW_CAPTURE_SMOKE_PY = "window_capture_smoke.py"
LIT_METRICS = "metrics"
LIT_QUALIFICATION_TOOL = "qualification_tool"
LIT_BMP = ".bmp"
LIT_COMMAND = "command"
LIT_IMAGE = "image"
LIT_PATH = "path"
LIT_IDENTITY = "identity"
LIT_LOG = "log"
LIT_POPULATED_ON = "populated_on"
LIT_MAIN = "__main__"


def caustic_log_text(preset=LIT_POPULATED, enabled=True, capture=False):
    distance = benchmark.caustic.PRESETS[preset]
    text = "\n".join((LIT_GRAPHICS_RUNTIME_CREATED_DEVICE_EXAMPLE_GPU, LIT_RENDERERSYSTEM_MATERIAL_GLASS_SELECTED, "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware", LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA,
        LIT_AVBOITTIMINGPROBE_IN_FLIGHT_RANGES_32, LIT_AVBOITTIMINGPROBE_RENDER_UNFOCUSED_1, "AvboitTimingProbe: caustic in-flight ranges 32", "CausticSphereSmokeProject: reflection mode 0", "CausticSphereSmokeProject: camera refraction disabled",
        "CausticSphereSmokeProject: caustics " + ("enabled" if enabled else LIT_DISABLED), "CausticTimingProbe: reflection diagnostics false temporal false spatial false feedback false", "CausticTimingProbe: scene single-static-sphere-ground-v1",
        f"CausticTimingProbe: camera {preset} distance {distance} height 0.85", "CausticTimingProbe: fixed delta 0.016666667 yaw 0 sphere scale 0.7", "CausticTimingProbe: directional pitch 0.9 yaw 0.65 intensity 2",
        "CausticTimingProbe: vertical FOV radians 1.0471976", "CausticTimingProbe: photon phases 2", LIT_RENDERERSYSTEM_DEFERRED_RENDERING_TARG, LIT_TRANSPARENTMULTISMOKEPROJECT_SHUTDOWN))
    if enabled:
        text += "\nRendererSystem: dispatched hardware caustic producer (131072 photons/frame, 2 temporal phases, 262144 full-grid budget, 1 caustic lights, 1 refractive instances)"
    if capture:
        text += "\nFramebufferCapture: capture ready\nFramebufferCapture: graphics source frame 359"
    return text


class CausticMeasurementTests(unittest.TestCase):
    def test_completed_resolve_and_photon_counts_cannot_be_divided_or_missing(self):
        workload = benchmark.workloads()[LIT_CAUSTIC_POPULATED]
        benchmark.validate_coverage(scopes(workload), workload, 6, 100)
        for name in (benchmark.caustic.RESOLVE, benchmark.caustic.PHOTONS):
            missing = scopes(workload)
            del missing[name]
            with self.subTest(scope=name), self.assertRaises(benchmark.SmokeFailure):
                benchmark.validate_coverage(missing, workload, 6, 100)
            wrong = scopes(workload)
            wrong[name][LIT_GPU_SAMPLES] *= 2
            with self.assertRaisesRegex(benchmark.SmokeFailure, LIT_SAMPLE_RATIO):
                benchmark.validate_coverage(wrong, workload, 6, 100)

    def test_warmup_is_actual_completed_gpu_work_not_publication_count(self):
        values = {name: {LIT_GPU_SAMPLES: 12, LIT_TOTAL_MS: 1, LIT_REPORTS: 2} for name in (benchmark.FRAME, benchmark.caustic.PHOTONS)}
        benchmark.caustic.validate_warmup(values)
        for name in values:
            changed = copy.deepcopy(values)
            changed[name][LIT_GPU_SAMPLES] = 11
            changed[name][LIT_REPORTS] = 1000
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_warmup(changed)

    def test_actual_native_route_geometry_and_photon_budget_are_load_bearing(self):
        for preset in benchmark.caustic.PRESETS:
            workload = benchmark.workloads()[LIT_CAUSTIC + preset]
            text = caustic_log_text(preset)
            workload.validate_log(text, workload, True)
            for changed in (text.replace("131072 photons", "65536 photons"), text.replace("262144 full-grid", "131072 full-grid"), text.replace("1 refractive", "2 refractive"),
                text.replace("height 0.85", "height 0.7"), text.replace("sphere scale 0.7", "sphere scale 0.35"), text.replace("intensity 2", "intensity 4"), text.replace("radians 1.0471976", "radians 0.7"),
                text.replace("photon phases 2", "photon phases 4"), text.replace("131072 photons/frame, 2 temporal phases", "65536 photons/frame, 4 temporal phases"), text.replace("yaw 0", "yaw 1"), text.replace("hardware caustic producer", "software caustic producer"), text + "\nCausticSphereSmokeProject: reflection mode 2",
                text + "\nAvboitTimingProbe: caustic in-flight ranges 2"):
                with self.subTest(preset=preset, changed=changed[-120:]), self.assertRaises(benchmark.SmokeFailure):
                    workload.validate_log(changed, workload, True)

    def test_caustic_current_route_rejects_retired_and_contradictory_routes(self):
        workload = benchmark.workloads()[LIT_CAUSTIC_POPULATED]
        settings = dict(workload.environment_overrides)
        text = caustic_log_text()
        hardware_marker = LIT_RENDERERSYSTEM_DISPATCHED_HARDWARE_TRA
        legacy = text.replace(LIT_NATURAL_HARDWARE_SHADOW_ROUTE, LIT_NATURAL_HYBRID_SHADOW_ROUTE).replace(hardware_marker, LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_log(legacy, settings)
        retired_route = next(line for line in legacy.splitlines() if LIT_NATURAL_HYBRID_SHADOW_ROUTE in line)
        for altered in (legacy, text + "\n" + retired_route):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                workload.validate_log(altered, workload, True)
        for altered in (text.replace(hardware_marker, ""), text + "\n" + LIT_RENDERERSYSTEM_DISPATCHED_SOFTWARE_SHA):
            with self.subTest(text=altered), self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_log(altered, settings)

    def test_capture_evidence_is_required_for_qualification_and_forbidden_for_timing(self):
        workload = benchmark.workloads()[LIT_CAUSTIC_POPULATED]
        text = caustic_log_text(capture=True)
        settings = dict(workload.environment_overrides)
        actual = benchmark.caustic.validate_log(text, settings, capture=True)
        self.assertEqual(actual["graphics_source_frame"], 359)
        for value in (text, caustic_log_text() + "\nVK_LAYER_KHRONOS_validation"):
            with self.assertRaises(benchmark.SmokeFailure):
                workload.validate_log(value, workload, True)
        for changed in (caustic_log_text(), text.replace("source frame 359", "source frame 7")):
            with self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_log(changed, settings, capture=True)
        off = benchmark.caustic.environment(LIT_POPULATED, False)
        benchmark.caustic.validate_log(caustic_log_text(enabled=False, capture=True), off, capture=True)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_log(text, off, capture=True)

    def test_receiver_mask_excludes_sphere_and_image_background(self):
        width, height = 160, 120
        points = list(benchmark.caustic.receiver_pixels(width, height, 2.2))
        self.assertGreater(len(points), 100)
        self.assertNotIn((width // 2, height // 2), points)
        self.assertTrue(all(y > height // 2 for _, y in points))
        off = (width, height, [[(50, 50, 50) for _ in range(width)] for _ in range(height)])
        rows = [list(row) for row in off[2]]
        for x, y in points[:20]:
            rows[y][x] = (70, 70, 70)
        for x in range(width):
            rows[0][x] = (255, 255, 255)
        result = benchmark.caustic.footprint((width, height, rows), off, 2.2)
        self.assertEqual(result[LIT_POSITIVE_PIXELS], 20)
        self.assertEqual(benchmark.caustic.footprint(off, off, 2.2)[LIT_POSITIVE_PIXELS], 0)

    def test_visible_pixel_and_tile_reduction_are_independent_qualification_gates(self):
        good = {LIT_POPULATED: {LIT_POSITIVE_PIXELS: 1000, LIT_POSITIVE_TILES: 100, LIT_POSITIVE_CHANNEL_GAIN: 60000}, LIT_SPARSE: {LIT_POSITIVE_PIXELS: 250, LIT_POSITIVE_TILES: 30, LIT_POSITIVE_CHANNEL_GAIN: 15000}}
        benchmark.caustic.validate_metrics(good)
        for key, value in ((LIT_POSITIVE_PIXELS, 99), (LIT_POSITIVE_CHANNEL_GAIN, 1499), (LIT_POSITIVE_TILES, 81)):
            changed = copy.deepcopy(good)
            changed[LIT_SPARSE][key] = value
            with self.subTest(key=key), self.assertRaises(benchmark.SmokeFailure):
                benchmark.caustic.validate_metrics(changed)
        with self.assertRaises(benchmark.SmokeFailure):
            benchmark.caustic.validate_metrics({LIT_POPULATED: good[LIT_POPULATED], LIT_SPARSE: good[LIT_POPULATED]})

    def test_caustic_cli_requires_both_frozen_arm_qualification_reports(self):
        common = [LIT_BASELINE_EXECUTABLE, LIT_A, LIT_BASELINE_RUNTIME, LIT_AR, LIT_BASELINE_SOURCE_MANIFEST, LIT_AS_JSON, LIT_CANDIDATE_EXECUTABLE, LIT_B, LIT_CANDIDATE_RUNTIME, LIT_BR, LIT_CANDIDATE_SOURCE_MANIFEST, LIT_BS_JSON,
            LIT_LOGSERVER_EXECUTABLE, LIT_LOGGER, LIT_OUTPUT_DIRECTORY, LIT_OUTPUT]
        proofs = ["--baseline-caustic-qualification", "aqual.json", "--candidate-caustic-qualification", "bqual.json"]
        for partial in ([], proofs[:2], proofs[2:]):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                benchmark.parse_args(common + [LIT_WORKLOAD, LIT_CAUSTIC_POPULATED] + partial)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark.parse_args(common + proofs)

    def test_qualification_replay_requires_exact_identity_policy_and_all_four_captures(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / LIT_QUALIFICATION_JSON
            identity = {LIT_FROZEN: "actual-arm"}
            document = {LIT_SCHEMA: benchmark.caustic.SCHEMA, LIT_POLICY: benchmark.caustic.POLICY, LIT_ARM: identity, LIT_CAPTURES: {}}
            for changed in ({**document, LIT_ARM: {LIT_FROZEN: "other-arm"}}, {**document, LIT_POLICY: {}}, document):
                path.write_text(json.dumps(changed), encoding=LIT_UTF_8)
                with self.assertRaises(benchmark.SmokeFailure):
                    benchmark.caustic.validate_report(path, identity)

    def test_caustic_rejected_output_preserves_existing_evidence_without_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            output = directory / "old_evidence"
            output.mkdir()
            saved = output / LIT_FAILURE_JSON
            saved.write_bytes(LIT_B_ORIGINAL_FAILURE_EVIDENCE)
            argv = ["--executable", str(directory / "bin" / LIT_FIXTURE_EXE), "--runtime", str(directory / LIT_RUNTIME), "--source-manifest", str(directory / LIT_SOURCE / LIT_SOURCE_JSON),
                LIT_LOGSERVER_EXECUTABLE, str(directory / LIT_LOGGER / LIT_LOGGER_EXE), LIT_OUTPUT_DIRECTORY, str(output)]
            with patch.object(benchmark.caustic.subprocess, "run") as launch, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(benchmark.caustic.main(argv), 1)
            launch.assert_not_called()
            self.assertEqual(saved.read_bytes(), LIT_B_ORIGINAL_FAILURE_EVIDENCE)
            self.assertEqual(sorted(path.name for path in output.iterdir()), [LIT_FAILURE_JSON])

    def test_caustic_output_overlap_rejected_in_both_directions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            protected = root / "inputs"
            for output in (protected, protected / LIT_OUTPUT, root):
                with self.subTest(output=output), self.assertRaises(benchmark.SmokeFailure):
                    benchmark.caustic.validate_output_path(output, (protected,))
            benchmark.caustic.validate_output_path(root / "evidence", (protected,))

    def test_caustic_requested_gpu_debug_needs_actual_loader_activation(self):
        utility = benchmark.caustic
        text = "\n".join(utility.GPU_DEBUG_MARKERS)
        utility.validate_gpu_debug(text, [LIT_GPUDBG])
        utility.validate_gpu_debug("ordinary launch", [])
        for marker in utility.GPU_DEBUG_MARKERS:
            with self.subTest(marker=marker), self.assertRaises(benchmark.SmokeFailure):
                utility.validate_gpu_debug(text.replace(marker, "requested only"), [LIT_GPUDBG])

    def test_synthetic_distinct_frozen_roots_replay_and_tampering_checks(self):
        # Small synthetic parser evidence only; this does not qualify a real framebuffer or GPU arm.
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            policy = {**benchmark.caustic.POLICY, "width": 160, "height": 120}
            with patch.multiple(benchmark.caustic, WIDTH=160, HEIGHT=120, POLICY=policy):
                utility = benchmark.caustic
                width, height = 160, 120
                launchers, sources = {}, {}
                launcher_bytes = Path(utility.__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY).read_bytes()
                for name in (LIT_BASELINE, LIT_CANDIDATE):
                    arm_root = directory / name
                    launcher = arm_root / "source/tests/smoke/window_capture_smoke.py"
                    launcher.parent.mkdir(parents=True)
                    launcher.write_bytes(launcher_bytes)
                    manifest = arm_root / LIT_SOURCE_JSON
                    manifest.write_text(json.dumps({LIT_REVISION: name, LIT_FILES: { launcher.relative_to(arm_root).as_posix(): hashlib.sha256(launcher_bytes).hexdigest()}}), encoding=LIT_UTF_8)
                    launchers[name] = launcher
                    sources[name] = benchmark.source_identity(manifest)
                identity = {"executable": str(directory / LIT_FIXTURE_EXE), LIT_RUNTIME: str(directory / LIT_RUNTIME), LIT_SOURCE: sources[LIT_CANDIDATE]}
                logger = directory / LIT_LOGGER_EXE
                logger.write_bytes(b"synthetic logger identity")
                (directory / "crash_handler.exe").write_bytes(b"synthetic crash helper identity")
                logger_dependency = directory / "logger_dependency.dll"
                logger_dependency.write_bytes(b"synthetic logger dependency identity")
                args = SimpleNamespace(**identity, logserver_executable=logger, timeout=90.0, application_arg=[])
                document = {LIT_SCHEMA: utility.SCHEMA, LIT_POLICY: policy, LIT_ARM: identity, LIT_CAPTURES: {}, LIT_METRICS: {}, LIT_QUALIFICATION_TOOL: benchmark.file_identity(Path(utility.__file__)),
                    "launcher": benchmark.file_identity(Path(utility.__file__).with_name(LIT_WINDOW_CAPTURE_SMOKE_PY)), "logserver": benchmark.file_identity(logger), "logserver_path": str(logger), "logserver_binaries": benchmark.binary_identity(logger),
                    "timeout_seconds": 90.0, "application_args": []}
                for preset, distance in utility.PRESETS.items():
                    frames = {}
                    for enabled in (True, False):
                        key = preset + ("_on" if enabled else "_off")
                        image, log = directory / (key + LIT_BMP), directory / (key + ".log")
                        selected = set(utility.receiver_pixels(width, height, distance)) if enabled else set()
                        pixels = bytearray()
                        for y in reversed(range(height)):
                            for x in range(width):
                                pixels.extend(bytes((70, 70, 70) if (x, y) in selected else (50, 50, 50)))
                        header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
                        header += struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
                        image.write_bytes(header + pixels)
                        log.write_text(caustic_log_text(preset, enabled, capture=True).replace(LIT_N_1280X900, "160x120"), encoding=LIT_UTF_8)
                        settings = utility.environment(preset, enabled)
                        document[LIT_CAPTURES][key] = {"settings": settings, LIT_RUNTIME: utility.validate_log(log.read_text(encoding=LIT_UTF_8), settings, capture=True), LIT_COMMAND: utility.capture_command(args, image, launcher=launchers[LIT_CANDIDATE]),
                            LIT_IMAGE: {LIT_PATH: image.name, LIT_IDENTITY: benchmark.file_identity(image)}, LIT_LOG: {LIT_PATH: log.name, LIT_IDENTITY: benchmark.file_identity(log)}}
                        frames[enabled] = utility.read_bmp_24_rows(image)
                    document[LIT_METRICS][preset] = utility.footprint(frames[True], frames[False], distance)
                report = directory / LIT_QUALIFICATION_JSON
                report.write_text(json.dumps(document), encoding=LIT_UTF_8)
                result, paths = utility.validate_report(report, identity)
                self.assertEqual(result[LIT_METRICS], document[LIT_METRICS])
                self.assertEqual(len(paths), 12)
                # Both physical frozen roots contain identical launchers; replay must bind the selected arm's path.
                baseline_identity = {**identity, LIT_SOURCE: sources[LIT_BASELINE]}
                baseline_document = copy.deepcopy(document)
                baseline_document[LIT_ARM] = baseline_identity
                for key, record in baseline_document[LIT_CAPTURES].items():
                    record[LIT_COMMAND] = utility.capture_command(args, directory / (key + LIT_BMP), launcher=launchers[LIT_BASELINE])
                report.write_text(json.dumps(baseline_document), encoding=LIT_UTF_8)
                baseline_result, _ = utility.validate_report(report, baseline_identity)
                self.assertEqual(baseline_result[LIT_METRICS], document[LIT_METRICS])
                wrong_root = copy.deepcopy(document)
                wrong_root[LIT_CAPTURES][LIT_POPULATED_ON][LIT_COMMAND] = baseline_document[LIT_CAPTURES][LIT_POPULATED_ON][LIT_COMMAND]
                report.write_text(json.dumps(wrong_root), encoding=LIT_UTF_8)
                with self.assertRaisesRegex(benchmark.SmokeFailure, "launch command changed"):
                    utility.validate_report(report, identity)
                unpinned_identity = copy.deepcopy(identity)
                unpinned_identity[LIT_SOURCE][LIT_FILES].pop(str(launchers[LIT_CANDIDATE].resolve()))
                unpinned = copy.deepcopy(document)
                unpinned[LIT_ARM] = unpinned_identity
                report.write_text(json.dumps(unpinned), encoding=LIT_UTF_8)
                with self.assertRaisesRegex(benchmark.SmokeFailure, "launcher is unpinned"):
                    utility.validate_report(report, unpinned_identity)
                report.write_text(json.dumps(document), encoding=LIT_UTF_8)
                launchers[LIT_CANDIDATE].write_bytes(launcher_bytes + LIT_MODIFIED.encode(LIT_UTF_8))
                with self.assertRaisesRegex(benchmark.SmokeFailure, "launcher is unpinned or its bytes changed"):
                    utility.validate_report(report, identity)
                launchers[LIT_CANDIDATE].write_bytes(launcher_bytes)
                saved_dependency = logger_dependency.read_bytes()
                logger_dependency.write_bytes(saved_dependency + LIT_MODIFIED.encode(LIT_UTF_8))
                with self.assertRaisesRegex(benchmark.SmokeFailure, "dependency inventory changed"):
                    utility.validate_report(report, identity)
                logger_dependency.write_bytes(saved_dependency)
                for kind in (LIT_IMAGE, LIT_LOG):
                    raw = directory / document[LIT_CAPTURES][LIT_POPULATED_ON][kind][LIT_PATH]
                    saved = raw.read_bytes()
                    raw.write_bytes(saved + LIT_MODIFIED.encode(LIT_UTF_8))
                    with self.subTest(kind=kind), self.assertRaisesRegex(benchmark.SmokeFailure, "raw .* changed"):
                        utility.validate_report(report, identity)
                    raw.write_bytes(saved)
                for field in (LIT_METRICS, LIT_QUALIFICATION_TOOL, LIT_COMMAND):
                    changed = copy.deepcopy(document)
                    if field == LIT_METRICS:
                        changed[field][LIT_POPULATED][LIT_POSITIVE_PIXELS] += 1
                    elif field == LIT_QUALIFICATION_TOOL:
                        changed[field]["sha256"] = "0" * 64
                    else:
                        command = changed[LIT_CAPTURES][LIT_POPULATED_ON][LIT_COMMAND]
                        command[command.index("--application-capture-frame-count") + 1] = LIT_N_16
                    report.write_text(json.dumps(changed), encoding=LIT_UTF_8)
                    with self.subTest(field=field), self.assertRaises(benchmark.SmokeFailure):
                        utility.validate_report(report, identity)


if __name__ == LIT_MAIN:
    unittest.main()
