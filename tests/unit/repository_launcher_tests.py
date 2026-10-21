import argparse
import ctypes
import importlib.util
import os
import platform
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

import launcher  # noqa: E402
from launcher import repository_windows_process  # noqa: E402
from repository_launcher_test_support import FakeSpawnedProcess


LIT_WINDOWSPROCESSAPI = "WindowsProcessApi"
LIT_REALPATH = "realpath"
LIT_GETPID = "getpid"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_GROUP = "group"
LIT_SOURCE = "source"
LIT_NAN = "nan"
LIT_INF = "inf"
LIT_INF_2 = "-inf"
LIT_VALIDATE_LAUNCH_PATHS = "validate_launch_paths"
LIT_STOP_EXISTING_PROCESS = "stop_existing_process"
LIT_ABSENT_RUNTIME = "absent_runtime"
LIT_RESOLVE_PROFILE_LOG_PORT = "resolve_profile_log_port"
LIT_RESOLVE_EXECUTABLE_PATH = "resolve_executable_path"
LIT_UNUSED = "unused"
LIT_RUN_WORKLOADS = "run_workloads"
LIT_LOG = "log"
LIT_OPEN = "open"
LIT_CLOSE = "close"
LIT_WAIT = "wait"
LIT_FORCE = "force"
LIT_RELEASE = "release"
LIT_TERMINATE = "terminate"
LIT_KILL = "kill"
LIT_NATIVE_WAIT = "native_wait"
LIT_WINDOWS = "windows"
LIT_WINDOWS_2 = "Windows"
LIT_AMD64 = "AMD64"
LIT_ARM64 = "arm64"
LIT_ARM64_2 = "ARM64"
LIT_SYSTEM = "system"
LIT_MACHINE = "machine"
LIT_QUERY_WINDOWS_NATIVE_MACHINE_NAME = "query_windows_native_machine_name"
LIT_REPO = "repo"
LIT_BUILD = "build"
LIT_FULL = "full"
LIT_DBG = "dbg"
LIT_TESTBED = "testbed"
LIT_UTF_8 = "utf-8"
LIT_RUN = "run"
LIT_C_BUILD_ARM64_VIEWER_EXE = r"C:\build\arm64\viewer.exe"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_LAUNCH_PY = "launch.py"
LIT_TESTS = "tests"
LIT_SMOKE = "smoke"
LIT_LAUNCHER_PY = "launcher.py"
LIT_AB = "ab"
LIT_ASYNC_SHADOW_M4 = "async_shadow_m4"
LIT_COMMAND_IR = "command_ir"
LIT_FRAME_LAGGED_ASYNC_LIGHTING = "frame_lagged_async_lighting"
LIT_RUN_PY = "run.py"
LIT_HARDWARE_SHADOW_BOUNDARY = "hardware_shadow_boundary"
LIT_TRANSFER_QUEUE = "transfer_queue"
LIT_UTILITIES = "utilities"
LIT_TEX_CONV = "tex_conv"
LIT_ASYNC_SHADOW_M4_2 = "async-shadow-m4"
LIT_SAME_NAME = "same_name"
LIT_A = r"\A"
LIT_Z = r"\Z"
LIT_CONFIG = "--config"
LIT_OPT = "opt"
LIT_MAIN = "__main__"
LIT_SKIP_BUILD = "--skip-build"
LIT_BUILD_DIR = "--build-dir"
LIT_CONFIGURE_PRESET = "--configure-preset"
LIT_WINDOWS_DLL = "WinDLL"
LIT_WINDOWS_LAST_ERROR = "get_last_error"
LIT_WINDOWS_ENV_ARCH6432 = "PROCESSOR_ARCHITEW6432"
LIT_STDOUT = "stdout"
LIT_STDERR = "stderr"
LIT_CHILD_STDOUT = "launcher child help"
LIT_CHILD_STDERR = "launcher child error"
LIT_PYTHON_COMMAND = "-c"
LIT_READ_WRITE_TEXT = "w+"
LIT_CHILD_STREAMS_PROGRAM = (f"import sys; print({LIT_CHILD_STDOUT!r}, flush=True); " f"print({LIT_CHILD_STDERR!r}, file=sys.stderr, flush=True); sys.exit(23)")


NAME_SYMBOL_CAPTURE_SPEC = importlib.util.spec_from_file_location("nwb_test_generate_name_symbols", ROOT / "tests" / "name_symbols" / "generate_name_symbols.py")
if NAME_SYMBOL_CAPTURE_SPEC is None or NAME_SYMBOL_CAPTURE_SPEC.loader is None:
    raise ImportError("Could not load the test-owned name-symbol capture helper")
generate_name_symbols = importlib.util.module_from_spec(NAME_SYMBOL_CAPTURE_SPEC)
NAME_SYMBOL_CAPTURE_SPEC.loader.exec_module(generate_name_symbols)


class FakeWindowsProcessApi:
    def __init__(self, image_paths, wait_results):
        self.image_paths = image_paths
        self.wait_results = {pid: list(results) for pid, results in wait_results.items()}
        self.handles = {}
        self.events = []

    def process_ids(self):
        return tuple(self.image_paths)

    def open_process(self, pid):
        handle = object()
        self.handles[pid] = handle
        self.events.append((LIT_OPEN, pid, handle))
        return handle

    def query_process_image_path(self, handle):
        self.events.append(("query", handle))
        return self.image_paths[next(pid for pid, value in self.handles.items() if value is handle)]

    def request_close(self, pid):
        self.events.append((LIT_CLOSE, pid))
        return True

    def wait_for_exit(self, handle, timeout_seconds):
        self.events.append((LIT_WAIT, handle, timeout_seconds))
        pid = next(pid for pid, value in self.handles.items() if value is handle)
        return self.wait_results[pid].pop(0)

    def force_terminate(self, handle):
        self.events.append((LIT_FORCE, handle))
        return True

    def exit_code(self, handle):
        return 0

    def close_process(self, handle):
        self.events.append((LIT_RELEASE, handle))


class FakeBoundedProcessApi:
    def __init__(self, wait_results, events, can_terminate=True):
        self.wait_results = list(wait_results)
        self.events = events
        self.handle = object()
        self.can_terminate = can_terminate

    def open_process(self, pid):
        self.events.append((LIT_OPEN, pid, self.handle))
        return self.handle

    def request_close(self, pid):
        self.events.append((LIT_CLOSE, pid))
        return True

    def wait_for_exit(self, handle, timeout_seconds):
        self.events.append((LIT_NATIVE_WAIT, handle, timeout_seconds))
        return self.wait_results.pop(0)

    def force_terminate(self, handle):
        self.assert_retained_handle(handle)
        self.events.append((LIT_FORCE, handle))
        return self.can_terminate

    def close_process(self, handle):
        self.events.append((LIT_RELEASE, handle))

    def assert_retained_handle(self, handle):
        if handle is not self.handle:
            raise AssertionError("hard termination did not use the retained process handle")


class LauncherPlatformTests(unittest.TestCase):
    def test_host_architecture_uses_native_windows_machine_under_emulation(self):
        with (mock.patch.object(platform, LIT_SYSTEM, return_value=LIT_WINDOWS_2), mock.patch.object(platform, LIT_MACHINE, return_value=LIT_AMD64), mock.patch.object(launcher.HostProbe, LIT_QUERY_WINDOWS_NATIVE_MACHINE_NAME, return_value=LIT_ARM64_2),
            mock.patch.dict(os.environ, {}, clear=True),):
            self.assertEqual(LIT_ARM64, launcher.host_arch_name())

    def test_missing_native_windows_api_rejects_environment_and_process_guesses(self):
        with (mock.patch.object(platform, LIT_SYSTEM, return_value=LIT_WINDOWS_2), mock.patch.object(platform, LIT_MACHINE, return_value=LIT_AMD64) as machine, mock.patch.object(ctypes, LIT_WINDOWS_DLL, return_value=mock.Mock(spec=()), create=True),
            mock.patch.dict(os.environ, {LIT_WINDOWS_ENV_ARCH6432: LIT_ARM64_2}, clear=True),):
            with self.assertRaisesRegex(SystemExit, launcher.WINDOWS_WOW64_PROC2):
                launcher.host_arch_name()
            machine.assert_not_called()

    def test_failed_native_windows_query_rejects_guesses_and_recovers_after_success(self):
        kernel32 = mock.Mock()
        kernel32.GetCurrentProcess.return_value = 1
        query = getattr(kernel32, launcher.WINDOWS_WOW64_PROC2)
        query.return_value = 0
        with (mock.patch.object(platform, LIT_SYSTEM, return_value=LIT_WINDOWS_2), mock.patch.object(platform, LIT_MACHINE, return_value=LIT_AMD64) as machine, mock.patch.object(ctypes, LIT_WINDOWS_DLL, return_value=kernel32, create=True),
            mock.patch.object(ctypes, LIT_WINDOWS_LAST_ERROR, return_value=87, create=True), mock.patch.dict(os.environ, {LIT_WINDOWS_ENV_ARCH6432: LIT_AMD64}, clear=True),):
            with self.assertRaisesRegex(SystemExit, "Windows error 87"):
                launcher.host_arch_name()

            def report_arm64(process, process_machine, native_machine):
                native_machine._obj.value = launcher.WINDOWS_IMAGE_FILE_MACHINE_ARM64
                return 1

            query.side_effect = report_arm64
            self.assertEqual(LIT_ARM64, launcher.host_arch_name())
            machine.assert_not_called()

    def test_unknown_native_windows_machine_is_rejected(self):
        kernel32 = mock.Mock()
        kernel32.GetCurrentProcess.return_value = 1

        def report_unknown(process, process_machine, native_machine):
            native_machine._obj.value = 0x014c
            return 1

        getattr(kernel32, launcher.WINDOWS_WOW64_PROC2).side_effect = report_unknown
        with mock.patch.object(ctypes, LIT_WINDOWS_DLL, return_value=kernel32, create=True):
            with self.assertRaisesRegex(SystemExit, "unsupported Windows native machine type 0x014c"):
                launcher.query_windows_native_machine_name()

    def test_explicit_architecture_must_match_configure_preset(self):
        args = argparse.Namespace(repo_root=Path(os.sep) / LIT_REPO, platform=LIT_WINDOWS, arch=LIT_ARM64, domain=None, configure_preset="windows-clang-x64", build_dir=None, config=LIT_DBG, cmake=None,)
        with self.assertRaisesRegex(SystemExit, "conflicts with configure preset"):
            launcher.resolve_launch_settings(args, LIT_FULL)


    def test_windows_handle_wrapper_failure_closes_unpublished_native_handle(self):
        for can_terminate in (False, True):
            with self.subTest(can_terminate=can_terminate):
                kernel32 = mock.Mock()
                kernel32.OpenProcess.side_effect = [707] if can_terminate else [0, 707]
                with mock.patch.object(ctypes, LIT_WINDOWS_DLL, side_effect=[kernel32, mock.Mock(), mock.Mock()], create=True):
                    api = repository_windows_process.WindowsProcessApi()
                failure = MemoryError("handle wrapper allocation failed")

                with mock.patch.object(repository_windows_process, "WindowsProcessHandle", side_effect=failure), self.assertRaises(MemoryError) as result:
                    api.open_process(708)

                self.assertIs(failure, result.exception)
                access = repository_windows_process.PROCESS_QUERY_WAIT_AND_TERMINATE_ACCESS
                expected = [mock.call(access, False, 708)]
                if not can_terminate:
                    expected.append(mock.call(repository_windows_process.PROCESS_QUERY_AND_WAIT_ACCESS, False, 708))
                self.assertEqual(expected, kernel32.OpenProcess.call_args_list)
                kernel32.CloseHandle.assert_called_once_with(707)

    def test_windows_kill_existing_matches_exact_image_not_same_basename(self):
        x64_image = r"C:\Build\x64\viewer.exe"
        arm64_image = r"C:\Build\arm64\viewer.exe"
        api = FakeWindowsProcessApi({ 101: x64_image, 202: r"c:\build\ARM64\viewer.exe", }, {202: [True]},)

        with (mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=api), mock.patch.object(os.path, LIT_REALPATH, side_effect=lambda path: path), mock.patch.object(os, LIT_GETPID, return_value=999)):
            results = repository_windows_process.stop_processes_by_image_path(arm64_image, 3.0, 2.0)

        self.assertEqual((202,), tuple(result.pid for result in results))
        self.assertIn((LIT_CLOSE, 202), api.events)
        self.assertNotIn((LIT_CLOSE, 101), api.events)
        self.assertFalse(any(event[0] == LIT_FORCE for event in api.events))

    def test_windows_existing_forced_shutdown_uses_retained_process_handle(self):
        api = FakeWindowsProcessApi({202: LIT_C_BUILD_ARM64_VIEWER_EXE}, {202: [False, True]},)

        with (mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=api), mock.patch.object(os.path, LIT_REALPATH, side_effect=lambda path: path), mock.patch.object(os, LIT_GETPID, return_value=999)):
            results = repository_windows_process.stop_processes_by_image_path(LIT_C_BUILD_ARM64_VIEWER_EXE, 3.0, 2.0)

        retained_handle = api.handles[202]
        self.assertTrue(results[0].forced)
        self.assertIn((LIT_FORCE, retained_handle), api.events)

    def test_windows_query_only_handle_can_still_complete_gracefully(self):
        events = []
        process = FakeSpawnedProcess(302, 0, events)

        with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, True], events, can_terminate=False)):
            result = repository_windows_process.run_bounded_process(process, 8.0, 4.0, 2.0)

        self.assertEqual(0, result.exit_code)
        self.assertFalse(result.forced)
        self.assertFalse(any(event[0] == LIT_FORCE for event in events))

    def test_windows_query_only_handle_reports_live_process_at_hard_boundary(self):
        events = []
        process = FakeSpawnedProcess(306, 0, events)

        with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "did not grant terminate access"):
            with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, False, False], events, can_terminate=False)):
                repository_windows_process.run_bounded_process(process, 8.0, 4.0, 2.0)

        retained_handle = next(event[2] for event in events if event[0] == LIT_OPEN)
        self.assertIn((LIT_FORCE, retained_handle), events)
        self.assertEqual(1, sum(event[0] == LIT_OPEN for event in events))


    def test_windows_bounded_shutdown_propagates_nonzero_graceful_exit(self):
        events = []
        process = FakeSpawnedProcess(304, 23, events)

        with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, True], events)):
            result = repository_windows_process.run_bounded_process(process, 8.0, 4.0, 2.0)

        self.assertEqual(23, result.exit_code)
        self.assertFalse(result.forced)
        self.assertNotIn((LIT_FORCE, 304), events)
        self.assertNotIn((LIT_TERMINATE, 304), events)
        self.assertNotIn((LIT_KILL, 304), events)

    def test_windows_bounded_shutdown_forces_exact_process_only_after_grace_timeout(self):
        events = []
        process = FakeSpawnedProcess(404, 7, events)

        with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, False, True], events)):
            result = repository_windows_process.run_bounded_process(process, 9.0, 5.0, 2.0)

        self.assertEqual(7, result.exit_code)
        self.assertTrue(result.forced)
        self.assertEqual([(LIT_OPEN, 404, mock.ANY), (LIT_NATIVE_WAIT, mock.ANY, 9.0), (LIT_CLOSE, 404), (LIT_NATIVE_WAIT, mock.ANY, 5.0), (LIT_FORCE, mock.ANY), (LIT_NATIVE_WAIT, mock.ANY, 2.0), (LIT_WAIT, None), (LIT_RELEASE, mock.ANY),], events,)

    def test_windows_bounded_shutdown_rejects_success_status_after_forced_termination(self):
        events = []
        process = FakeSpawnedProcess(405, 0, events)

        with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "forced process .* successful"):
            with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, False, True], events)):
                repository_windows_process.run_bounded_process(process, 9.0, 5.0, 2.0)

        grace_wait_index = next(index for index, event in enumerate(events) if event[0] == LIT_NATIVE_WAIT and event[2] == 5.0)
        force_index = next(index for index, event in enumerate(events) if event[0] == LIT_FORCE)
        self.assertLess(grace_wait_index, force_index)

    def test_launcher_propagates_bounded_windows_process_exit_status(self):
        events = []
        process = FakeSpawnedProcess(406, 23, events)
        args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=False, run_seconds=9.0,)
        run_result = repository_windows_process.WindowsBoundedRunResult(406, 23, True, True, False)

        with (mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch.object(launcher, LIT_HOST_PLATFORM_NAME, return_value=LIT_WINDOWS),
            mock.patch.object(repository_windows_process, "run_bounded_process", return_value=run_result) as bounded, mock.patch.object(launcher, LIT_TERMINATE_PROCESS) as generic_terminate,):
            exit_code = launcher.launch_process(args, Path(LIT_C_BUILD_ARM64_VIEWER_EXE), Path(r"C:\build\arm64"), {}, (), paths_validated=True,)

        self.assertEqual(23, exit_code)
        bounded.assert_called_once_with(process, 9.0, launcher.APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS, launcher.APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,)
        generic_terminate.assert_not_called()

    def test_owned_child_output_reaches_redirected_parent_streams(self):
        args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=False, run_seconds=None,)

        with (tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryFile(mode=LIT_READ_WRITE_TEXT, encoding=LIT_UTF_8) as stdout, tempfile.TemporaryFile(mode=LIT_READ_WRITE_TEXT, encoding=LIT_UTF_8) as stderr,):
            with (mock.patch.object(sys, LIT_STDOUT, stdout), mock.patch.object(sys, LIT_STDERR, stderr),):
                exit_code = launcher.launch_process(args, Path(sys.executable), Path(temp_dir), os.environ.copy(), (LIT_PYTHON_COMMAND, LIT_CHILD_STREAMS_PROGRAM),)

            stdout.seek(0)
            stderr.seek(0)
            stdout_lines = stdout.read().splitlines()
            stderr_lines = stderr.read().splitlines()

        self.assertEqual(23, exit_code)
        self.assertIn(LIT_CHILD_STDOUT, stdout_lines)
        self.assertNotIn(LIT_CHILD_STDERR, stdout_lines)
        self.assertEqual([LIT_CHILD_STDERR], stderr_lines)

    def test_ignores_nonstandard_leaf_script_names(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            category = root / LIT_TESTS
            category.mkdir(parents=True)
            directory = root / LIT_TESTS / LIT_SMOKE
            directory.mkdir(parents=True)
            (directory / LIT_LAUNCHER_PY).write_text("", encoding=LIT_UTF_8)
            (directory / LIT_RUN_PY).write_text("", encoding=LIT_UTF_8)

            launchers = launcher.discover_repo_launchers(root)

        self.assertEqual({}, launchers)

    def test_directory_discovery_only_returns_direct_children(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            paths = (root / LIT_TESTS / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_ASYNC_SHADOW_M4 / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_COMMAND_IR / LIT_LAUNCH_PY,
                root / LIT_TESTS / LIT_AB / LIT_FRAME_LAGGED_ASYNC_LIGHTING / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_HARDWARE_SHADOW_BOUNDARY / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_TRANSFER_QUEUE / LIT_LAUNCH_PY,
                root / LIT_TESTS / LIT_SMOKE / LIT_LAUNCH_PY,)
            for path in paths:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)

            tests_launchers = launcher.discover_directory_launchers(Path(LIT_TESTS), root)
            ab_launchers = launcher.discover_directory_launchers(Path(LIT_TESTS) / LIT_AB, root)

        self.assertEqual({LIT_AB, LIT_SMOKE}, set(tests_launchers))
        self.assertNotIn(LIT_ASYNC_SHADOW_M4_2, tests_launchers)
        self.assertIn(LIT_ASYNC_SHADOW_M4_2, ab_launchers)

    def test_generated_source_copies_do_not_hide_current_authored_launchers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            category = root / LIT_TESTS
            authored = (category / LIT_LAUNCH_PY, category / LIT_SMOKE / LIT_LAUNCH_PY, category / LIT_GROUP / LIT_LAUNCH_PY, category / LIT_GROUP / LIT_SOURCE / LIT_LAUNCH_PY)
            generated = tuple(path for artifact in launcher.LAUNCHER_ARTIFACT_DIRECTORIES for path in (category / artifact / LIT_LAUNCH_PY, category / artifact / "frozen" / LIT_TESTS / LIT_SMOKE / LIT_LAUNCH_PY,
                category / LIT_SMOKE / artifact / "old" / LIT_LAUNCH_PY))
            for path in (*authored, *generated):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)
            generated_bytes = {path: path.read_bytes() for path in generated}

            direct = launcher.discover_directory_launchers(Path(LIT_TESTS), root)
            leaves = launcher.discover_repo_launchers(root)

            self.assertEqual({LIT_SMOKE, LIT_GROUP}, set(direct))
            self.assertEqual({LIT_SMOKE, LIT_SOURCE}, set(leaves))
            self.assertEqual(Path(LIT_TESTS) / LIT_GROUP / LIT_SOURCE / LIT_LAUNCH_PY, leaves[LIT_SOURCE].script)
            self.assertEqual(generated_bytes, {path: path.read_bytes() for path in generated})

    def test_duplicate_launch_commands_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for category in (root / LIT_TESTS, root / LIT_UTILITIES):
                category.mkdir(parents=True)
                (category / LIT_LAUNCH_PY).write_text("", encoding=LIT_UTF_8)
            for path in (root / LIT_TESTS / LIT_SAME_NAME / LIT_LAUNCH_PY, root / LIT_UTILITIES / LIT_SAME_NAME / LIT_LAUNCH_PY,):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(SystemExit, "duplicate launch command 'same-name'"):
                launcher.discover_repo_launchers(root)

    def test_category_with_leaf_launchers_requires_a_router(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            path = root / LIT_UTILITIES / LIT_TEX_CONV / LIT_LAUNCH_PY
            path.parent.mkdir(parents=True)
            path.write_text("", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(SystemExit, LIT_A + re.escape(f"missing category launcher: {Path(LIT_UTILITIES) / LIT_LAUNCH_PY}") + LIT_Z,):
                launcher.discover_repo_launchers(root)

    def test_nested_leaf_requires_an_intermediate_router(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for path in (root / LIT_TESTS / LIT_LAUNCH_PY, root / LIT_TESTS / LIT_AB / LIT_ASYNC_SHADOW_M4 / LIT_LAUNCH_PY,):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("", encoding=LIT_UTF_8)

            with self.assertRaisesRegex(SystemExit, LIT_A + re.escape(f"missing directory launcher: {Path(LIT_TESTS) / LIT_AB / LIT_LAUNCH_PY}") + LIT_Z,):
                launcher.discover_repo_launchers(root)


class LauncherDurationBoundaryTests(unittest.TestCase):
    def test_cli_rejects_nonfinite_and_nonpositive_durations(self):
        for option in ("--run-seconds", "--profile-logserver-timeout"):
            for value in (LIT_NAN, LIT_INF, LIT_INF_2, "0", "-1"):
                with self.subTest(option=option, value=value), mock.patch("sys.stderr"), self.assertRaises(SystemExit):
                    launcher.make_parser({}).parse_args([LIT_RUN, LIT_TESTBED, f"{option}={value}"])

    def test_direct_invalid_run_duration_cannot_validate_stop_or_spawn(self):
        for duration in (float(LIT_NAN), float(LIT_INF), float(LIT_INF_2), 0.0, -1.0):
            args = argparse.Namespace(run_seconds=duration, kill_existing=True, dry_run=False, detach=False, gpudbg=False)
            with self.subTest(duration=duration), mock.patch.object(launcher, LIT_VALIDATE_LAUNCH_PATHS) as validate, mock.patch.object(launcher, LIT_STOP_EXISTING_PROCESS) as stop, mock.patch.object(subprocess, LIT_POPEN) as spawn:
                with self.assertRaises(SystemExit):
                    launcher.launch_process(args, Path("absent_application"), Path(LIT_ABSENT_RUNTIME), {}, ())
                validate.assert_not_called()
                stop.assert_not_called()
                spawn.assert_not_called()

    def test_direct_invalid_profile_timeout_cannot_resolve_or_spawn_server(self):
        for duration in (float(LIT_NAN), float(LIT_INF), float(LIT_INF_2), 0.0, -1.0):
            args = argparse.Namespace(with_profile=True, profile_logserver_timeout=duration)
            with self.subTest(duration=duration), mock.patch.object(launcher, LIT_RESOLVE_PROFILE_LOG_PORT) as port, mock.patch.object(launcher, LIT_RESOLVE_EXECUTABLE_PATH) as resolve, mock.patch.object(subprocess, LIT_POPEN) as spawn:
                with self.assertRaises(SystemExit):
                    launcher.start_profile_session(args, mock.sentinel.settings, Path(LIT_ABSENT_RUNTIME), {})
                port.assert_not_called()
                resolve.assert_not_called()
                spawn.assert_not_called()


class NameSymbolCaptureBoundaryTests(unittest.TestCase):
    def test_stale_sidecar_cleanup_failure_blocks_workloads_and_preserves_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            capture = root / "capture"
            destination = root / "published"
            capture.mkdir()
            destination.mkdir()
            stale = capture / "probe.namesym"
            published = destination / stale.name
            stale_bytes = b"stale source symbols"
            published_bytes = b"previous valid symbols"
            fresh_bytes = b"freshly captured symbols"
            stale.write_bytes(stale_bytes)
            published.write_bytes(published_bytes)
            arguments = ["--source-dir", str(root), LIT_CONFIGURE_PRESET, LIT_UNUSED, "--build-preset", LIT_UNUSED, LIT_BUILD_DIR, str(root / LIT_BUILD), LIT_CONFIG, LIT_OPT,
                "--buildmode-bin-dir", str(capture), "--dest", str(destination), "--run", "probe", LIT_SKIP_BUILD,]
            removal_error = PermissionError("sidecar is locked")
            with (mock.patch.object(generate_name_symbols.os, "remove", side_effect=removal_error) as remove, mock.patch.object(generate_name_symbols, LIT_RUN_WORKLOADS) as workloads,
                mock.patch.object(generate_name_symbols, "collect_sidecars", wraps=generate_name_symbols.collect_sidecars) as collect, mock.patch.object(generate_name_symbols, LIT_LOG) as log,):
                self.assertEqual(1, generate_name_symbols.main(arguments))
                remove.assert_called_once_with(str(stale))
                workloads.assert_not_called()
                collect.assert_not_called()
                log.assert_any_call(generate_name_symbols.MSG_STALE_REMOVE_FAIL.format(str(stale), removal_error))
            self.assertEqual(stale_bytes, stale.read_bytes())
            self.assertEqual(published_bytes, published.read_bytes())
            self.assertEqual([published], list(destination.iterdir()))

            def capture_fresh_sidecar(_arguments):
                self.assertFalse(stale.exists())
                stale.write_bytes(fresh_bytes)

            with (mock.patch.object(generate_name_symbols, LIT_RUN_WORKLOADS, side_effect=capture_fresh_sidecar) as workloads, mock.patch.object(generate_name_symbols, LIT_LOG),):
                self.assertEqual(0, generate_name_symbols.main(arguments))
                workloads.assert_called_once()
            self.assertEqual(fresh_bytes, published.read_bytes())


if __name__ == LIT_MAIN:
    unittest.main()
