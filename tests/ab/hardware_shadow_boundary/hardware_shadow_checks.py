from __future__ import annotations

import tempfile
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from gpu_timing_parse import ScopeSummary, parse_timing_file, summarize_scopes
from hardware_shadow_report import HardwareShadowReport, RunResult, FRAME_SCOPE, OPAQUE_TRACE_SCOPE, TRANSPARENT_RESOLVE_SCOPE, TRANSPARENT_TRACE_SCOPE
from harness_test_support import make_abnormal_exit_terminator
from name_symbols import debug_name_hash_token
from window_capture_smoke import SmokeFailure


LIT_SELF_TEST_LOG_TIMEOUT_SECONDS = 0.1
LIT_SYS_STDERR = "sys.stderr"
LIT_APP_STOP = "app-stop"
LIT_CLEANUP_NONE = "cleanup-none"
LIT_LOGSERVER_LOG = "logserver_*.log"
LIT_HEALTHY_BENCHMARK = "healthy benchmark"
LIT_LOGSERVER_HELPER = "logserver-helper"
LIT_CAPABILITY_LOG = "capability.log"
LIT_HEALTHY_LOG = "healthy.log"
LIT_HEALTHY_BMP = "healthy.bmp"
LIT_BASELINE_LOG = "baseline.log"
LIT_BASELINE_BMP = "baseline.bmp"


class HardwareShadowBoundaryChecks:
    def __init__(self, harness):
        self.harness = harness

    def check_cli(self):
        harness = self.harness
        defaults = harness.parse_args([harness.LIT_SELF_TEST])
        numeric_arguments = [harness.LIT_HEALTHY_EXECUTABLE, harness.LIT_HEALTHY, harness.LIT_BASELINE_EXECUTABLE_2, harness.LIT_BASELINE, harness.LIT_RUNTIME_DIR, 'runtime', harness.LIT_OUTPUT_DIR, 'output']
        numeric_options = (harness.LIT_WARMUP_SECONDS, harness.LIT_MEASURE_SECONDS, harness.LIT_STARTUP_TIMEOUT, harness.LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN_2)
        for option in numeric_options:
            for value in ('nan', 'inf', '-inf'):
                with mock.patch(LIT_SYS_STDERR):
                    try:
                        harness.parse_args(numeric_arguments + [f'{option}={value}'])
                    except SystemExit as error:
                        assert error.code == 2
                    else:
                        raise AssertionError(f'hardware-shadow CLI accepted non-finite {option}={value}')
        for option in (harness.LIT_WARMUP_SECONDS, harness.LIT_MAXIMUM_TRANSPARENT_FRAME_REGRESSION_PERCEN_2):
            assert getattr(harness.parse_args(numeric_arguments + [f'{option}=0']), option[2:].replace('-', '_')) == 0.0
        for option in (harness.LIT_MEASURE_SECONDS, harness.LIT_STARTUP_TIMEOUT):
            with mock.patch(LIT_SYS_STDERR):
                try:
                    harness.parse_args(numeric_arguments + [f'{option}=0'])
                except SystemExit as error:
                    assert error.code == 2
                else:
                    raise AssertionError(f'hardware-shadow CLI accepted zero {option}')
            assert getattr(harness.parse_args(numeric_arguments + [f'{option}=1']), option[2:].replace('-', '_')) == 1.0
        return defaults

    def check_process_and_report(self, defaults):
        harness = self.harness
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module = harness
            executable = root / 'orchestration.exe'
            executable.write_bytes(b'exe')
            defaults.output_dir = root
            defaults.runtime_dir = root
            app = SimpleNamespace(pid=4321, poll=lambda: None)
            logserver = object()
            baseline = {root / 'old.log': 7}
            backend = mock.Mock()
            backend.wait_for_window.return_value = 17
            events = []
            terminate = make_abnormal_exit_terminator(app, events)

            def shutdown(process, log_directory, received_baseline, pattern, shutdown_name='logserver'):
                assert process is logserver
                assert events == [(LIT_APP_STOP, LIT_HEALTHY_BENCHMARK, 17)]
                events.append((LIT_LOGSERVER_HELPER, shutdown_name))
                return harness.CAPABILITY_SKIP_LOG
            with mock.patch.object(module, 'build_launch_environment', return_value={}), mock.patch.object(module, 'launch_logserver', return_value=(logserver, 49152, root, baseline, LIT_LOGSERVER_LOG)), mock.patch.object(module, 'launch_testbed', return_value=app), mock.patch.object(module, 'wait_for_log_message', return_value=harness.CAPABILITY_SKIP_LOG), mock.patch.object(module.CaptureProcessTermination, 'terminate_process', side_effect=terminate) as terminate_mock, mock.patch.object(module, 'shutdown_logserver_and_collect', side_effect=shutdown):
                try:
                    harness.run_single_arm(defaults, harness.LIT_HEALTHY, executable, {}, ('ready',), (), backend)
                except SmokeFailure as error:
                    assert 'exit 7' in str(error)
                else:
                    raise AssertionError('hardware-shadow orchestration accepted an abnormal Testbed exit')
            assert events == [(LIT_APP_STOP, LIT_HEALTHY_BENCHMARK, 17), (LIT_LOGSERVER_HELPER, harness.LIT_HARDWARE_SHADOW_BENCHMARK_LOGSERVER), (LIT_CLEANUP_NONE, LIT_HEALTHY_BENCHMARK, 17), (LIT_CLEANUP_NONE, harness.LIT_HARDWARE_SHADOW_BENCHMARK_LOGSERVER, None)]
            assert terminate_mock.mock_calls == [mock.call(app, LIT_HEALTHY_BENCHMARK, 17), mock.call(None, LIT_HEALTHY_BENCHMARK, 17), mock.call(None, harness.LIT_HARDWARE_SHADOW_BENCHMARK_LOGSERVER)]
            timing = root / 'timing.txt'
            timing_symbols = harness.load_name_symbols(None)
            frame_token = debug_name_hash_token(FRAME_SCOPE)
            visibility_token = debug_name_hash_token(harness.LIT_RENDER_SHADOW_VISIBILITY)
            opaque_token = debug_name_hash_token(OPAQUE_TRACE_SCOPE)
            transparent_trace_token = debug_name_hash_token(TRANSPARENT_TRACE_SCOPE)
            transparent_resolve_token = debug_name_hash_token(TRANSPARENT_RESOLVE_SCOPE)
            warmup_timing = f'=== interval: 20 frames / 0.5s ===\n  {frame_token}: window_avg_ms=4.0000 window_min_ms=3.0 window_max_ms=5.0 published_windows=20 total_ms=80 gpu_samples=40 sample_avg_ms=2\n  {visibility_token}: window_avg_ms=1.2500 window_min_ms=1.0 window_max_ms=1.5 published_windows=20 total_ms=25 gpu_samples=40 sample_avg_ms=0.625\n  {opaque_token}: window_avg_ms=0.5000 window_min_ms=0.4 window_max_ms=0.6 published_windows=20 total_ms=10 gpu_samples=40 sample_avg_ms=0.25\n  {transparent_trace_token}: window_avg_ms=0.3000 window_min_ms=0.2 window_max_ms=0.4 published_windows=20 total_ms=6 gpu_samples=40 sample_avg_ms=0.15\n  {transparent_resolve_token}: window_avg_ms=0.2000 window_min_ms=0.1 window_max_ms=0.3 published_windows=20 total_ms=4 gpu_samples=40 sample_avg_ms=0.1\n'
            measurement_timing = f'=== interval: 20 frames / 0.5s ===\n  {frame_token}: window_avg_ms=5.0000 window_min_ms=4.0 window_max_ms=6.0 published_windows=20 total_ms=100 gpu_samples=40 sample_avg_ms=2.5\n  {visibility_token}: window_avg_ms=1.7500 window_min_ms=1.0 window_max_ms=2.0 published_windows=20 total_ms=35 gpu_samples=40 sample_avg_ms=0.875\n  {opaque_token}: window_avg_ms=0.6000 window_min_ms=0.5 window_max_ms=0.7 published_windows=20 total_ms=12 gpu_samples=40 sample_avg_ms=0.3\n  {transparent_trace_token}: window_avg_ms=0.4000 window_min_ms=0.3 window_max_ms=0.5 published_windows=20 total_ms=8 gpu_samples=40 sample_avg_ms=0.2\n  {transparent_resolve_token}: window_avg_ms=0.3000 window_min_ms=0.2 window_max_ms=0.4 published_windows=20 total_ms=6 gpu_samples=40 sample_avg_ms=0.15\n'
            timing.write_bytes((warmup_timing + measurement_timing).encode(harness.LIT_UTF_8))
            scopes = summarize_scopes(parse_timing_file(timing, timing_symbols))
            measurement_offset = len(warmup_timing.encode(harness.LIT_UTF_8))
            measurement_scopes = summarize_scopes(parse_timing_file(timing, timing_symbols, measurement_offset))
            assert measurement_scopes[FRAME_SCOPE].median_ms == 5.0
            assert measurement_scopes[FRAME_SCOPE].sample_count == 1
            capability_log = root / LIT_CAPABILITY_LOG
            capability_log.write_text(harness.CAPABILITY_SKIP_LOG, encoding=harness.LIT_UTF_8)
            running_process = SimpleNamespace(poll=lambda: None)
            assert harness.wait_for_log_message(running_process, root, {}, LIT_CAPABILITY_LOG, harness.HEALTHY_REQUIRED_LOGS[0], LIT_SELF_TEST_LOG_TIMEOUT_SECONDS, harness.CAPABILITY_SKIP_LOG) == harness.CAPABILITY_SKIP_LOG
            healthy_scopes = {name: replace(summary, sample_count=11, positive_sample_count=11) for name, summary in scopes.items()}
            healthy_scopes[FRAME_SCOPE] = ScopeSummary(11, 11, 4.95, 4.95, 4.9, 5.0)
            healthy = RunResult(mode=harness.LIT_HEALTHY, executable=harness.LIT_HEALTHY, timing_file=str(timing), log_file=LIT_HEALTHY_LOG, capture_file=LIT_HEALTHY_BMP, scopes=healthy_scopes, missing_required_log_messages=[], runtime_forbidden_log_messages=[], semantic_forbidden_log_messages=[])
            baseline_scopes = {FRAME_SCOPE: ScopeSummary(11, 11, 4.5, 4.5, 4.4, 4.6), OPAQUE_TRACE_SCOPE: ScopeSummary(11, 11, 0.55, 0.55, 0.5, 0.6)}
            baseline = RunResult(mode=harness.LIT_BASELINE, executable=harness.LIT_BASELINE, timing_file=str(timing), log_file=LIT_BASELINE_LOG, capture_file=LIT_BASELINE_BMP, scopes=baseline_scopes, missing_required_log_messages=[], runtime_forbidden_log_messages=[], semantic_forbidden_log_messages=[])
            args = SimpleNamespace(minimum_samples=2, maximum_transparent_frame_regression_percent=None, frozen_yaw=0.6, warmup_seconds=4.0, measure_seconds=12.0, gpu_validation=False)
            reporter = HardwareShadowReport(args)
            report = reporter.evaluate_runs(healthy, baseline)
            assert report[harness.LIT_VERDICT] == harness.LIT_PASS
            baseline.scopes = {**baseline_scopes, OPAQUE_TRACE_SCOPE: ScopeSummary(12, 10, 0.55, 0.55, 0.0, 0.6)}
            assert reporter.evaluate_runs(healthy, baseline)[harness.LIT_VERDICT] == harness.LIT_INCONCLUSIVE
            baseline.runtime_forbidden_log_messages = ['VUID']
            assert reporter.evaluate_runs(healthy, baseline)[harness.LIT_VERDICT] == harness.LIT_FAIL
            baseline.scopes = baseline_scopes
            baseline.runtime_forbidden_log_messages = []
            args.maximum_transparent_frame_regression_percent = 5.0
            threshold_report = reporter.evaluate_runs(healthy, baseline)
            assert threshold_report[harness.LIT_VERDICT] == harness.LIT_FAIL
            baseline_with_transparent_timing = RunResult(mode=harness.LIT_BASELINE, executable=harness.LIT_BASELINE, timing_file=str(timing), log_file=LIT_BASELINE_LOG, capture_file=LIT_BASELINE_BMP, scopes={**baseline_scopes, TRANSPARENT_TRACE_SCOPE: ScopeSummary(2, 2, 0.2, 0.2, 0.1, 0.3)}, missing_required_log_messages=[], runtime_forbidden_log_messages=[], semantic_forbidden_log_messages=[])
            assert reporter.evaluate_runs(healthy, baseline_with_transparent_timing)[harness.LIT_VERDICT] == harness.LIT_FAIL
            healthy_without_resolve = RunResult(mode=harness.LIT_HEALTHY, executable=harness.LIT_HEALTHY, timing_file=str(timing), log_file=LIT_HEALTHY_LOG, capture_file=LIT_HEALTHY_BMP, scopes={name: summary for name, summary in healthy_scopes.items() if name != TRANSPARENT_RESOLVE_SCOPE}, missing_required_log_messages=[], runtime_forbidden_log_messages=[], semantic_forbidden_log_messages=[])
            try:
                reporter.evaluate_runs(healthy_without_resolve, baseline)
            except SmokeFailure as error:
                assert TRANSPARENT_RESOLVE_SCOPE in str(error)
            else:
                raise AssertionError('hardware transparent evidence must require transparent resolve timing')
            try:
                reporter.require_positive_scope_samples({OPAQUE_TRACE_SCOPE: ScopeSummary(2, 0, 0.0, 0.0, 0.0, 0.0)}, OPAQUE_TRACE_SCOPE, 2, timing, harness.LIT_HEALTHY)
            except SmokeFailure as error:
                assert 'did not publish at least 2 positive intervals' in str(error)
            else:
                raise AssertionError('zero-duration route timing must not satisfy hardware transparent evidence')
