"""Offline self-test for the renderer-baseline A/B runner: BMP round trips, pixel diffing, corpus loading and verification, and capture orchestration with faked processes."""


from __future__ import annotations


import struct
import sys
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


LIT_F = "f"
LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS = "NWB_GI_SMOKE_MIN_SETTLE_SECONDS"
LIT_CAPTURE_SOURCE_FRAME = "capture_source_frame"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT"
LIT_SETTLE_SECONDS_2 = "--settle-seconds"
LIT_STARTUP_TIMEOUT = "--startup-timeout"
LIT_MAXIMUM_MEAN_ABS_2 = "--maximum-mean-abs"
LIT_SURFEL_GI_COMPLEX = "surfel-gi-complex"
LIT_GRAPHICS_SOURCE_FRAME_902 = "graphics source frame 902"
LIT_GRAPHICS_SOURCE_FRAME_900 = "graphics source frame 900"
LIT_NAN = "nan"
LIT_INF = "inf"
LIT_TEST_SOURCE = "test-source"
LIT_SETTLED = "settled"
LIT_RENDERER_BASELINE_CAPTURE = "renderer baseline capture"
LIT_RENDERER_BASELINE_LOGSERVER = "renderer baseline logserver"
LIT_UTF_8 = "utf-8"
LIT_SCHEMA = "schema"
LIT_CAPTURE_KIND = "capture_kind"
LIT_PROFILE = "profile"
LIT_CAPTURE_MODE = "capture_mode"
LIT_CAPTURE_FREEZE_FRAME = "capture_freeze_frame"
LIT_FIXED_DELTA_SECONDS = "fixed_delta_seconds"
LIT_SETTLE_SECONDS = "settle_seconds"
LIT_GPU_VALIDATION = "gpu_validation"
LIT_SOURCE_REVISION = "source_revision"
LIT_SOURCE_WORKTREE_CLEAN = "source_worktree_clean"
LIT_CAPTURE_FILE = "capture_file"
LIT_CAPTURE_SHA256 = "capture_sha256"
LIT_FROZEN_ENVIRONMENT = "frozen_environment"
LIT_REFERENCE_SOURCE_REVISION = "reference_source_revision"
LIT_MANIFEST_JSON = "manifest.json"
LIT_BASELINE = "baseline"
LIT_CORPUS_ID = "corpus_id"
LIT_ARTIFACT_ROOT = "artifact_root"
LIT_PROFILES = "profiles"
LIT_CAPTURE_ENVIRONMENT = "capture_environment"
LIT_REFERENCE = "reference"
LIT_DIRECTORY = "directory"
LIT_RUNTIME_LOG = "runtime.log"
LIT_MANIFEST_SHA256 = "manifest_sha256"
LIT_RUNTIME_LOG_SHA256 = "runtime_log_sha256"
LIT_LIMITS = "limits"
LIT_MAXIMUM_MAX_ABS = "maximum_max_abs"
LIT_MAXIMUM_MEAN_ABS = "maximum_mean_abs"
LIT_MAXIMUM_CHANGED_FRACTION = "maximum_changed_fraction"
LIT_SAME_REVISION_RECAPTURE = "same_revision_recapture"
LIT_COMPARISON_JSON = "comparison.json"
LIT_COMPARISON_SHA256 = "comparison_sha256"
LIT_NWB_RENDERER_BASELINE_COMPARISON_V1 = "nwb.renderer-baseline-comparison.v1"
LIT_CANDIDATE_SOURCE_REVISION = "candidate_source_revision"
LIT_DIFFERENCE = "difference"
LIT_MAX_ABS = "max_abs"
LIT_MEAN_ABS = "mean_abs"
LIT_CHANGED_FRACTION = "changed_fraction"
LIT_CANDIDATE_BMP = "candidate.bmp"
LIT_BASELINE_BMP = "baseline.bmp"
LIT_DIFFERENCE_BMP = "difference.bmp"
LIT_PROFILE_2 = "--profile"
LIT_EXECUTABLE = "--executable"
LIT_RUNTIME_DIR = "--runtime-dir"
LIT_OUTPUT_DIR = "--output-dir"
LIT_OPAQUE_TEXTURE = "opaque-texture"
LIT_APP_STOP = "app-stop"
LIT_CLEANUP_NONE = "cleanup-none"
LIT_LOGSERVER_LOG = "logserver_*.log"
LIT_LOGSERVER_HELPER = "logserver-helper"
LIT_CAPTURED_RUNTIME_EVIDENCE = "captured runtime evidence"
LIT_BUILD_LAUNCH_ENVIRONMENT = "build_launch_environment"
LIT_CREATE_CAPTURE_BACKEND = "create_capture_backend"
LIT_LAUNCH_LOGSERVER = "launch_logserver"
LIT_LAUNCH_TESTBED = "launch_testbed"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_SHUTDOWN_LOGSERVER_AND_COLLECT = "shutdown_logserver_and_collect"
LIT_PREPARE = "prepare"
LIT_CLOSE = "close"
LIT_CAPTURE = "capture"
LIT_SOFT_SHADOWS = "soft-shadows"
LIT_READY = "ready"
LIT_SETTLE = "settle"
LIT_TEST_REFERENCE = "test-reference"


def run_self_test(harness) -> int:
    complex_profile = harness.get_profile(LIT_SURFEL_GI_COMPLEX)
    complex_environment = dict(complex_profile.frozen_environment)
    complex_evidence = (
        "GiTestSmokeProject: complex GI live warmup start graphics_frame=361 presentations=360 required_presentations=360 required_seconds=30\n"
        "GiTestSmokeProject: complex GI live warmup end graphics_frame=901 presentations=900 elapsed_seconds=30.125\n"
        "FramebufferCapture: capture ready\n"
        "FramebufferCapture: graphics source frame 902\n"
        "GiTestSmokeProject: complex GI capture source_frame=902\n"
    )
    assert harness.validate_complex_gi_warmup(complex_evidence, complex_profile, complex_environment)[LIT_CAPTURE_SOURCE_FRAME] == 902
    for old, replacement in (
        ("start graphics_frame=361 presentations=360", "start graphics_frame=361 presentations=359"),
        ("elapsed_seconds=30.125", "elapsed_seconds=29.999999"),
        ("elapsed_seconds=30.125", "elapsed_seconds=nan"),
        ("elapsed_seconds=30.125", "elapsed_seconds=invalid"),
        ("elapsed_seconds=30.125", "elapsed_seconds=3_0.125"),
        ("required_seconds=30", "required_seconds=10"),
        ("end graphics_frame=901 presentations=900", "end graphics_frame=361 presentations=900"),
        ("end graphics_frame=901 presentations=900", "end graphics_frame=901 presentations=360"),
        ("capture source_frame=902", "capture source_frame=901"),
        (LIT_GRAPHICS_SOURCE_FRAME_902, LIT_GRAPHICS_SOURCE_FRAME_900),
    ):
        try:
            harness.validate_complex_gi_warmup(complex_evidence.replace(old, replacement), complex_profile, complex_environment)
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError(f"complex GI accepted invalid live warmup evidence: {replacement}")
    for line in complex_evidence.splitlines(keepends=True):
        if "capture ready" in line:
            continue
        for altered in (complex_evidence.replace(line, ""), complex_evidence + line):
            try:
                harness.validate_complex_gi_warmup(altered, complex_profile, complex_environment)
            except harness.SmokeFailure:
                pass
            else:
                raise AssertionError("complex GI accepted missing or repeated live warmup evidence")
    for value in ("10", LIT_NAN, LIT_INF, "invalid", "1e300"):
        try:
            harness.validate_complex_gi_warmup(complex_evidence, complex_profile, {**complex_environment, LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS: value})
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError("complex GI accepted an invalid configured live warmup")
    rounded_requirement = struct.unpack(LIT_F, struct.pack(LIT_F, 30.00002))[0]
    rounded_evidence = complex_evidence.replace("required_seconds=30", f"required_seconds={rounded_requirement!r}").replace("elapsed_seconds=30.125", f"elapsed_seconds={rounded_requirement!r}")
    harness.validate_complex_gi_warmup(rounded_evidence, complex_profile, {**complex_environment, LIT_NWB_GI_SMOKE_MIN_SETTLE_SECONDS: "30.00002"})
    for altered in ("".join(reversed(complex_evidence.splitlines(keepends=True))), complex_evidence.replace(LIT_GRAPHICS_SOURCE_FRAME_902, LIT_GRAPHICS_SOURCE_FRAME_900).replace("capture source_frame=902", "capture source_frame=900")):
        try:
            harness.validate_complex_gi_warmup(altered, complex_profile, complex_environment)
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError("complex GI accepted reordered or stale readback evidence")
    numeric_arguments = [LIT_PROFILE_2, LIT_OPAQUE_TEXTURE, LIT_EXECUTABLE, "renderer", LIT_RUNTIME_DIR, "runtime", LIT_OUTPUT_DIR, "output"]
    for option in (LIT_STARTUP_TIMEOUT, LIT_SETTLE_SECONDS_2, LIT_MAXIMUM_MEAN_ABS_2):
        for value in (LIT_NAN, LIT_INF, "-inf"):
            try:
                harness.validate_args(harness.make_parser().parse_args(numeric_arguments + [f"{option}={value}"]))
            except harness.SmokeFailure:
                pass
            else:
                raise AssertionError(f"renderer baseline CLI accepted non-finite {option}={value}")
    for option in (LIT_STARTUP_TIMEOUT, LIT_SETTLE_SECONDS_2):
        try:
            harness.validate_args(harness.make_parser().parse_args(numeric_arguments + [f"{option}=0"]))
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError(f"renderer baseline CLI accepted zero {option}")
    harness.validate_args(harness.make_parser().parse_args(numeric_arguments + ["--maximum-mean-abs=0"]))
    harness.validate_args(harness.make_parser().parse_args(numeric_arguments + ["--startup-timeout=1", "--settle-seconds=1"]))
    try:
        harness.validate_runtime_log("", ())
    except harness.SmokeFailure:
        pass
    else:
        raise AssertionError("renderer baseline empty runtime-log evidence must fail closed")
    with tempfile.TemporaryDirectory(prefix="nwb_renderer_baseline_") as temporary_directory:
        root = Path(temporary_directory)
        module = harness
        executable = root / "orchestration.exe"
        executable.write_bytes(b"exe")
        capture_path = root / "orchestration.bmp"
        runtime_log_path = root / "orchestration.log"
        app = SimpleNamespace(pid=4321, poll=lambda: None)
        logserver = object()
        baseline = {root / "old.log": 7}
        backend = mock.Mock()
        backend.wait_for_window.return_value = 17

        def capture(_window, path):
            path.write_bytes(b"bmp")
            return SimpleNamespace(appears_empty_or_white=False, has_pixel_variation=True)

        backend.capture_window.side_effect = capture
        orchestration_args = SimpleNamespace(executable=executable, runtime_dir=root, no_logserver=False, logserver_executable=None, startup_timeout=1.0, gpu_validation=False, settle_seconds=0.0, profile=LIT_OPAQUE_TEXTURE, reject_log=[],)
        events = []

        terminate = harness.make_abnormal_exit_terminator(app, events)

        def shutdown(process, directory, received_baseline, pattern, shutdown_name="logserver"):
            assert process is logserver
            assert events == [(LIT_APP_STOP, LIT_RENDERER_BASELINE_CAPTURE, 17)]
            events.append((LIT_LOGSERVER_HELPER, shutdown_name))
            return LIT_CAPTURED_RUNTIME_EVIDENCE

        with mock.patch.object(module, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
             mock.patch.object(module, LIT_CREATE_CAPTURE_BACKEND, return_value=backend), \
             mock.patch.object(module, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, LIT_LAUNCH_TESTBED, return_value=app), \
             mock.patch.object(module, LIT_TERMINATE_PROCESS, side_effect=terminate) as terminate_mock, \
             mock.patch.object(module, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, side_effect=shutdown):
            try:
                harness.capture_scene(orchestration_args, harness.get_profile(LIT_OPAQUE_TEXTURE), capture_path, runtime_log_path, {})
            except harness.SmokeFailure as error:
                assert "exit 7" in str(error)
            else:
                raise AssertionError("renderer orchestration accepted an abnormal Testbed exit")

        assert events == [(LIT_APP_STOP, LIT_RENDERER_BASELINE_CAPTURE, 17), (LIT_LOGSERVER_HELPER, LIT_RENDERER_BASELINE_LOGSERVER), (LIT_CLEANUP_NONE, LIT_RENDERER_BASELINE_CAPTURE, 17), (LIT_CLEANUP_NONE, LIT_RENDERER_BASELINE_LOGSERVER, None),]
        assert terminate_mock.mock_calls == [mock.call(app, LIT_RENDERER_BASELINE_CAPTURE, 17), mock.call(None, LIT_RENDERER_BASELINE_CAPTURE, 17), mock.call(None, LIT_RENDERER_BASELINE_LOGSERVER),]
        backend.close.assert_called_once_with()

        raw_backend = object.__new__(harness.WindowsCapture)
        raw_events = []
        raw_backend.wait_for_window = mock.Mock(return_value=17)
        raw_backend.prepare_raw_client_window = lambda window: raw_events.append((LIT_PREPARE, window))
        raw_backend.prepare_window = mock.Mock(side_effect=AssertionError("raw preparation required"))
        raw_backend.capture_window = mock.Mock(side_effect=AssertionError("prepared raw capture required"))
        raw_backend.close = lambda: raw_events.append((LIT_CLOSE,))

        def raw_capture(window, path):
            raw_events.append((LIT_CAPTURE, window))
            return capture(window, path)

        raw_backend.capture_prepared_raw_client_window = raw_capture
        raw_args = SimpleNamespace(**vars(orchestration_args))
        raw_args.profile = LIT_SOFT_SHADOWS
        with mock.patch.object(module, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
             mock.patch.object(module, LIT_CREATE_CAPTURE_BACKEND, return_value=raw_backend), \
             mock.patch.object(module, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, LIT_LAUNCH_TESTBED, return_value=app), \
             mock.patch.object(module, "wait_for_log_message", side_effect=lambda *args: raw_events.append((LIT_READY,))), \
             mock.patch.object(time, "sleep", side_effect=lambda delay: raw_events.append((LIT_SETTLE, delay))), \
             mock.patch.object(module, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
             mock.patch.object(module, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=LIT_CAPTURED_RUNTIME_EVIDENCE), \
             mock.patch.object(module, "validate_runtime_log", return_value=()), \
             mock.patch.object(module, LIT_SOURCE_REVISION, return_value=LIT_TEST_SOURCE), \
             mock.patch.object(module, LIT_SOURCE_WORKTREE_CLEAN, return_value=True):
            harness.capture_scene(raw_args, harness.get_profile(LIT_SOFT_SHADOWS), capture_path, runtime_log_path, {})
        assert raw_events == [(LIT_PREPARE, 17), (LIT_READY,), (LIT_SETTLE, 0.0), (LIT_CAPTURE, 17), (LIT_CLOSE,)]
        raw_backend.prepare_window.assert_not_called()
        raw_backend.capture_window.assert_not_called()

        # A completed readback must qualify its own source; desktop capture cannot replace missing provenance.
        complex_args = SimpleNamespace(**vars(orchestration_args))
        complex_args.profile = LIT_SURFEL_GI_COMPLEX
        complex_capture = root / "complex.bmp"
        complex_log = root / "complex.log"
        captured_environment = {}

        def launch_complex(_args, _executable, environment, _port):
            captured_environment.update(environment)
            return app

        def complete_complex(_app, output_path, _timeout):
            harness.write_bmp_rgb(output_path, 2, 1, bytes((0, 0, 0, 255, 0, 255)))
            return 0, True

        with mock.patch.object(module, LIT_BUILD_LAUNCH_ENVIRONMENT, return_value={}), \
             mock.patch.object(module, LIT_CREATE_CAPTURE_BACKEND, side_effect=AssertionError("complex GI must use application readback")), \
             mock.patch.object(module, LIT_LAUNCH_LOGSERVER, return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), \
             mock.patch.object(module, LIT_LAUNCH_TESTBED, side_effect=launch_complex), \
             mock.patch.object(module, "wait_for_application_capture_exit", side_effect=complete_complex), \
             mock.patch.object(module, LIT_TERMINATE_PROCESS, return_value=(0, "")), \
             mock.patch.object(module, LIT_SHUTDOWN_LOGSERVER_AND_COLLECT, return_value=complex_evidence), \
             mock.patch.object(module, LIT_SOURCE_REVISION, return_value=LIT_TEST_SOURCE), \
             mock.patch.object(module, LIT_SOURCE_WORKTREE_CLEAN, return_value=True):
            result = harness.capture_scene(complex_args, complex_profile, complex_capture, complex_log, complex_environment)
        assert result.live_warmup[LIT_CAPTURE_SOURCE_FRAME] == 902
        assert captured_environment[LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH] == str(complex_capture.resolve())
        assert captured_environment[LIT_NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_CO] == "360"

        reference_directory = root / LIT_OPAQUE_TEXTURE / LIT_REFERENCE
        reference_directory.mkdir(parents=True)
        reference_image = reference_directory / LIT_BASELINE_BMP
        candidate_image = root / LIT_CANDIDATE_BMP
        harness.write_bmp_rgb(reference_image, 2, 1, bytes((8, 16, 24, 32, 40, 48)))
        harness.write_bmp_rgb(candidate_image, 2, 1, bytes((8, 16, 24, 35, 40, 50)))
        manifest = { LIT_SCHEMA: harness.SCHEMA, LIT_CAPTURE_KIND: LIT_BASELINE, LIT_PROFILE: LIT_OPAQUE_TEXTURE, LIT_CAPTURE_FILE: LIT_BASELINE_BMP, LIT_CAPTURE_SHA256: harness.sha256_file(reference_image), LIT_FROZEN_ENVIRONMENT: {}, LIT_GPU_VALIDATION: False,
            LIT_SETTLE_SECONDS: 4.0, LIT_CAPTURE_MODE: LIT_SETTLED, LIT_CAPTURE_FREEZE_FRAME: 0, LIT_FIXED_DELTA_SECONDS: 0.0, LIT_SOURCE_REVISION: LIT_TEST_REFERENCE, }
        harness.write_json(reference_directory / LIT_MANIFEST_JSON, manifest)
        (reference_directory / LIT_RUNTIME_LOG).write_text("test runtime log\n", encoding=LIT_UTF_8)
        recapture_directory = root / LIT_OPAQUE_TEXTURE / "recapture"
        recapture_directory.mkdir(parents=True)
        comparison_path = recapture_directory / LIT_COMPARISON_JSON
        harness.write_json(comparison_path, { LIT_SCHEMA: LIT_NWB_RENDERER_BASELINE_COMPARISON_V1, LIT_PROFILE: LIT_OPAQUE_TEXTURE, LIT_REFERENCE_SOURCE_REVISION: LIT_TEST_REFERENCE, LIT_CANDIDATE_SOURCE_REVISION: LIT_TEST_REFERENCE, LIT_DIFFERENCE: { LIT_MAX_ABS: 2,
                    LIT_MEAN_ABS: 0.5, LIT_CHANGED_FRACTION: 0.25, }, },)
        corpus_path = root / "corpus.json"
        corpus = { LIT_SCHEMA: harness.CORPUS_SCHEMA, LIT_CORPUS_ID: harness.CURRENT_CORPUS_ID, LIT_ARTIFACT_ROOT: ".", LIT_CAPTURE_ENVIRONMENT: {LIT_GPU_VALIDATION: False}, LIT_PROFILES: { LIT_OPAQUE_TEXTURE: { LIT_REFERENCE: { LIT_DIRECTORY: "opaque-texture/reference",
                        LIT_SOURCE_REVISION: LIT_TEST_REFERENCE, LIT_CAPTURE_SHA256: harness.sha256_file(reference_image), LIT_MANIFEST_SHA256: harness.sha256_file(reference_directory / LIT_MANIFEST_JSON),
                        LIT_RUNTIME_LOG_SHA256: harness.sha256_file(reference_directory / LIT_RUNTIME_LOG), }, LIT_SAME_REVISION_RECAPTURE: { LIT_DIRECTORY: "opaque-texture/recapture", LIT_COMPARISON_SHA256: harness.sha256_file(comparison_path), LIT_MAX_ABS: 2,
                        LIT_MEAN_ABS: 0.5, LIT_CHANGED_FRACTION: 0.25, }, LIT_LIMITS: { LIT_MAXIMUM_MAX_ABS: 3, LIT_MAXIMUM_MEAN_ABS: 1.0, LIT_MAXIMUM_CHANGED_FRACTION: 0.75, }, } }, }
        harness.write_json(corpus_path, corpus)
        _, _, corpus_reference = harness.load_corpus_reference(harness.CURRENT_CORPUS_ID, root, LIT_OPAQUE_TEXTURE, harness.get_profile(LIT_OPAQUE_TEXTURE), False, corpus_path=corpus_path,)
        for key in (LIT_MAXIMUM_MAX_ABS, LIT_MAXIMUM_MEAN_ABS, LIT_MAXIMUM_CHANGED_FRACTION):
            for value in (float(LIT_NAN), float(LIT_INF), -float(LIT_INF), 1 << 4096):
                corpus[LIT_PROFILES][LIT_OPAQUE_TEXTURE][LIT_LIMITS][key] = value
                harness.write_json(corpus_path, corpus)
                try:
                    harness.load_corpus_reference(harness.CURRENT_CORPUS_ID, root, LIT_OPAQUE_TEXTURE, harness.get_profile(LIT_OPAQUE_TEXTURE), False, corpus_path=corpus_path)
                except harness.SmokeFailure:
                    pass
                else:
                    raise AssertionError(f"renderer baseline corpus accepted non-finite {key}")
            corpus[LIT_PROFILES][LIT_OPAQUE_TEXTURE][LIT_LIMITS][key] = corpus_reference.limits[key]
        harness.write_json(corpus_path, corpus)
        reference_log = reference_directory / LIT_RUNTIME_LOG
        reference_log.write_text("tampered runtime log\n", encoding=LIT_UTF_8)
        try:
            harness.load_corpus_reference(harness.CURRENT_CORPUS_ID, root, LIT_OPAQUE_TEXTURE, harness.get_profile(LIT_OPAQUE_TEXTURE), False, corpus_path=corpus_path,)
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError("corpus artifact tampering must fail closed")
        reference_log.write_text("test runtime log\n", encoding=LIT_UTF_8)
        corpus_args = SimpleNamespace(maximum_max_abs=None, maximum_mean_abs=None, maximum_changed_fraction=None,)
        harness.apply_corpus_limits(corpus_args, corpus_reference)
        corpus_args.maximum_max_abs = 4
        try:
            harness.apply_corpus_limits(corpus_args, corpus_reference)
        except harness.SmokeFailure:
            pass
        else:
            raise AssertionError("corpus comparison limits must not be relaxed")
        difference = harness.compare_bmp_rgb(reference_image, candidate_image, root / LIT_DIFFERENCE_BMP)
        args = SimpleNamespace(require_exact=False, maximum_max_abs=2, maximum_mean_abs=None, maximum_changed_fraction=None)
        assert harness.difference_failures(args, difference) == ["max abs 3 exceeds 2"]
        args.require_exact = True
        assert harness.difference_failures(args, difference)
    print("renderer-baseline runner self-test passed")
    return 0
