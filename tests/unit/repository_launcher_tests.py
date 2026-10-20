import argparse
import ctypes
import importlib.util
import json
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
LIT_SERVER = "server"
LIT_LAUNCHED = "launched "
LIT_HTTP_LOCALHOST = "http://localhost"
LIT_APPLICATION = "application"
LIT_LAUNCHER_PROCESS_SHUTIL_WHICH = "launcher.process.shutil.which"
LIT_START_PROFILE_SESSION = "start_profile_session"
LIT_REQUESTED_STOP_REFUSED = "requested stop refused"
LIT_CMAKE_COMMAND = "cmake_command"
LIT_BUILD_ENVIRONMENT = "build_environment"
LIT_ENSURE_FILE_API_QUERY = "ensure_file_api_query"
LIT_BUILD_2 = "--build"
LIT_APPLICATION_ARGUMENT = "application argument"
LIT_RETURN_VALUE = "return_value"
LIT_SIDE_EFFECT = "side_effect"
LIT_CMAKE_FILE_API_METADATA_IS_REQUIRED = "CMake File API metadata is required"
LIT_UNUSED = "unused"
LIT_RUN_WORKLOADS = "run_workloads"
LIT_LOG = "log"

NAME_SYMBOL_CAPTURE_SPEC = importlib.util.spec_from_file_location("nwb_test_generate_name_symbols", ROOT / "tests" / "name_symbols" / "generate_name_symbols.py")
if NAME_SYMBOL_CAPTURE_SPEC is None or NAME_SYMBOL_CAPTURE_SPEC.loader is None:
    raise ImportError("Could not load the test-owned name-symbol capture helper")
generate_name_symbols = importlib.util.module_from_spec(NAME_SYMBOL_CAPTURE_SPEC)
NAME_SYMBOL_CAPTURE_SPEC.loader.exec_module(generate_name_symbols)

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
LIT_CMAKE = "__cmake"
LIT_CMAKE_EXECUTABLE = "cmake"
LIT_BUILD = "build"
LIT_FULL = "full"
LIT_WINDOWS_CLANG_ARM64 = "windows-clang-arm64"
LIT_DBG = "dbg"
LIT_TESTBED = "testbed"
LIT_NWB_ASSET_BUILDER = "nwb_asset_builder"
LIT_CMAKECACHE_TXT = "CMakeCache.txt"
LIT_UTF_8 = "utf-8"
LIT_RUN = "run"
LIT_C_BUILD_ARM64_VIEWER_EXE = r"C:\build\arm64\viewer.exe"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_PIPELINE = "pipeline"
LIT_LAUNCH_PY = "launch.py"
LIT_COOLSTUFF = "CoolStuff"
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
LIT_DRY_RUN = "--dry-run"
LIT_CONFIG = "--config"
LIT_ASSET_ROOT = "--asset-root"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_RUNTIME_RESOURCES = "runtime resources"
LIT_EMPTY = "--"
LIT_OPT = "opt"
LIT_REPO_ROOT = "--repo-root"
LIT_CACHE_DIRECTORY = "--cache-directory"
LIT_CUSTOM_ARTIFACTS = "custom artifacts"
LIT_DEPENDENCY_COMPUTER_EXE = "dependency_computer.exe"
LIT_ASSET_BUILDER_EXE = "asset_builder.exe"
LIT_ASSET_GATHERER_EXE = "asset_gatherer.exe"
LIT_BUILTINS_PRINT = "builtins.print"
LIT_MAIN = "__main__"
LIT_BUILD_ONLY = "--build-only"
LIT_SKIP_BUILD = "--skip-build"
LIT_BUILD_DIR = "--build-dir"
LIT_CONFIGURE_PRESET = "--configure-preset"
LIT_ARCH = "--arch"
LIT_PLATFORM = "--platform"
LIT_DOMAIN = "--domain"
LIT_WITH_PROFILE = "--with-profile"
LIT_LIBRARY_TARGET = "nwb_common"
LIT_AGGREGATE_TARGET = "nwb_pipeline"
LIT_COLD_CUSTOM_BUILD = "cold custom build"
LIT_HELP = "--help"
LIT_FORBIDDEN_LAUNCH = "A build operation entered a launch path"
LIT_ENGINE = "engine"
LIT_WINDOWS_CLANG_ENGINE_ARM64 = "windows-clang-engine-arm64"
LIT_WINDOWS_CLANG_TESTBED_ARM64 = "windows-clang-testbed-arm64"
LIT_EXEC_OUTPUT_ROOT = "__exec"
LIT_WINDOWS_DLL = "WinDLL"
LIT_WINDOWS_LAST_ERROR = "get_last_error"
LIT_WINDOWS_ENV_ARCH6432 = "PROCESSOR_ARCHITEW6432"
LIT_METADATA_INDEX = "index-0001.json"
LIT_METADATA_CODEMODEL = "codemodel.json"
LIT_METADATA_TARGET = "target.json"
LIT_METADATA_LIBRARY = "STATIC_LIBRARY"
LIT_ACTUAL_EXECUTABLE = "actual-target.exe"
LIT_PREVIEW_EXECUTABLE = "preview-target"
LIT_EXPLICIT_EXECUTABLE = "explicit-target.exe"
LIT_STDOUT = "stdout"
LIT_STDERR = "stderr"
LIT_CHILD_STDOUT = "launcher child help"
LIT_CHILD_STDERR = "launcher child error"
LIT_PYTHON_COMMAND = "-c"
LIT_READ_WRITE_TEXT = "w+"
LIT_CHILD_STREAMS_PROGRAM = (f"import sys; print({LIT_CHILD_STDOUT!r}, flush=True); " f"print({LIT_CHILD_STDERR!r}, file=sys.stderr, flush=True); sys.exit(23)")


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


class FakeSpawnedProcess:
    def __init__(self, pid, exit_code, events):
        self.pid = pid
        self.exit_code = exit_code
        self.events = events
        self.returncode = None

    def wait(self, timeout=None):
        self.events.append((LIT_WAIT, timeout))
        self.returncode = self.exit_code
        return self.exit_code

    def poll(self):
        return self.returncode

    def terminate(self):
        self.events.append((LIT_TERMINATE, self.pid))

    def kill(self):
        self.events.append((LIT_KILL, self.pid))


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


class LauncherProcessStopTests(unittest.TestCase):
    def test_profile_spawn_diagnostic_failure_reaps_unpublished_server(self):
        events = []
        process = FakeSpawnedProcess(701, 0, events)
        failure = BrokenPipeError("closed launcher output")
        args = argparse.Namespace(with_profile=True, profile_logserver_timeout=1.0, profile_logserver_target=LIT_SERVER, profile_logserver_executable=None, profile_logserver_name=None, profile_logserver_args=[], dry_run=False)

        def print_status(message, **_options):
            if str(message).startswith(LIT_LAUNCHED):
                raise failure

        with (mock.patch.object(launcher, LIT_RESOLVE_PROFILE_LOG_PORT, return_value=12345), mock.patch.object(launcher, LIT_RESOLVE_EXECUTABLE_PATH, return_value=Path(LIT_SERVER)), mock.patch.object(launcher, LIT_VALIDATE_LAUNCH_PATHS),
            mock.patch.object(launcher, "wait_for_tcp_port") as ready, mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch(LIT_BUILTINS_PRINT, side_effect=print_status)):
            with self.assertRaises(BrokenPipeError) as result:
                launcher.start_profile_session(args, mock.sentinel.settings, ROOT, {})

        self.assertIs(failure, result.exception)
        self.assertEqual([(LIT_TERMINATE, 701), (LIT_WAIT, launcher.PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS)], events)
        self.assertEqual(0, process.returncode)
        ready.assert_not_called()

    def test_cleanup_diagnostic_failure_still_forces_and_reaps_process(self):
        process = mock.Mock(pid=702)
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired(LIT_SERVER, 1.0), 9]
        failure = BrokenPipeError("closed cleanup output")

        with mock.patch(LIT_BUILTINS_PRINT, side_effect=failure), self.assertRaises(BrokenPipeError) as result:
            launcher.terminate_process(process, LIT_SERVER, 1.0)

        self.assertIs(failure, result.exception)
        self.assertEqual([mock.call.poll(), mock.call.terminate(), mock.call.wait(timeout=1.0), mock.call.kill(), mock.call.wait()], process.mock_calls)

    def test_application_spawn_diagnostic_failure_reaps_application_and_server(self):
        for detach in (False, True):
            with self.subTest(detach=detach):
                events = []
                process = FakeSpawnedProcess(703, 0, events)
                server = FakeSpawnedProcess(704, 0, events)
                session = launcher.ProfileSession(12345, Path(LIT_SERVER), server)
                args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=detach, run_seconds=None, profile_log_address=LIT_HTTP_LOCALHOST)
                failure = BrokenPipeError("closed application output")

                def print_status(message, **_options):
                    if str(message).startswith(LIT_LAUNCHED):
                        raise failure

                with mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch(LIT_BUILTINS_PRINT, side_effect=print_status), self.assertRaises(BrokenPipeError) as result:
                    launcher.launch_process(args, Path(LIT_APPLICATION), ROOT, {}, (), session, paths_validated=True)

                self.assertIs(failure, result.exception)
                self.assertEqual([(LIT_TERMINATE, 703), (LIT_WAIT, launcher.PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS), (LIT_TERMINATE, 704), (LIT_WAIT, launcher.PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS)], events)
                self.assertEqual(0, process.returncode)
                self.assertEqual(0, server.returncode)

    def test_explicit_detach_and_keyboard_interrupt_retain_live_application_and_server(self):
        for detach in (False, True):
            with self.subTest(detach=detach):
                process = mock.Mock(pid=705)
                process.poll.return_value = None
                process.wait.side_effect = KeyboardInterrupt
                server = mock.Mock(pid=706)
                session = launcher.ProfileSession(12345, Path(LIT_SERVER), server)
                args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=detach, run_seconds=None, profile_log_address=LIT_HTTP_LOCALHOST)

                with mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch(LIT_BUILTINS_PRINT):
                    self.assertEqual(0, launcher.launch_process(args, Path(LIT_APPLICATION), ROOT, {}, (), session, paths_validated=True))

                self.assertEqual(0 if detach else 1, process.wait.call_count)
                process.terminate.assert_not_called()
                process.kill.assert_not_called()
                server.terminate.assert_not_called()
                server.kill.assert_not_called()
                server.wait.assert_not_called()

    def test_no_matching_posix_process_does_not_prevent_launch(self):
        with (mock.patch(LIT_LAUNCHER_PROCESS_SHUTIL_WHICH, return_value="/usr/bin/pkill"), mock.patch.object(subprocess, LIT_RUN, return_value=mock.Mock(returncode=1)),):
            launcher.stop_existing_process(Path("absent-application"), launcher.PLATFORM_LINUX)

    def test_posix_stop_failure_prevents_another_application_and_bounds_diagnostic(self):
        diagnostic = b"permission denied " + b"x" * launcher.PKILL_STDERR_MAX_BYTES + b"UNBOUNDED_TAIL"
        args = argparse.Namespace(dry_run=False, kill_existing=True)
        for exit_code in (0, 1, 2, 3, -9):
            with self.subTest(exit_code=exit_code):
                def fail_stop(*_arguments, **options):
                    options[LIT_STDERR].write(diagnostic)
                    return mock.Mock(returncode=exit_code)

                with (mock.patch(LIT_LAUNCHER_PROCESS_SHUTIL_WHICH, return_value="/usr/bin/pkill"), mock.patch.object(subprocess, LIT_RUN, side_effect=fail_stop), mock.patch.object(launcher, LIT_VALIDATE_LAUNCH_PATHS),
                    mock.patch.object(launcher, LIT_HOST_PLATFORM_NAME, return_value=launcher.PLATFORM_LINUX), mock.patch.object(subprocess, LIT_POPEN) as spawn,):
                    with self.assertRaisesRegex(SystemExit, "permission denied") as failure:
                        launcher.launch_process(args, Path(LIT_APPLICATION), ROOT, {}, [])
                    self.assertNotIn("UNBOUNDED_TAIL", str(failure.exception))
                    spawn.assert_not_called()

    def test_missing_posix_stop_tool_rejects_requested_stop(self):
        with mock.patch(LIT_LAUNCHER_PROCESS_SHUTIL_WHICH, return_value=None):
            with self.assertRaisesRegex(SystemExit, "requires pkill"):
                launcher.stop_existing_process(Path(LIT_APPLICATION), launcher.PLATFORM_LINUX)

    def test_stop_failure_closes_started_profile_session_before_application_spawn(self):
        for detach in (False, True):
            with self.subTest(detach=detach):
                args = argparse.Namespace(dry_run=False, kill_existing=True, detach=detach)
                profile_process = mock.Mock()
                session = launcher.ProfileSession(12345, Path("logserver"), profile_process)
                with (mock.patch.object(launcher, LIT_VALIDATE_LAUNCH_PATHS), mock.patch.object(launcher, LIT_START_PROFILE_SESSION, return_value=session) as start_profile, mock.patch.object(launcher, LIT_HOST_PLATFORM_NAME, return_value=launcher.PLATFORM_LINUX),
                    mock.patch.object(launcher, LIT_STOP_EXISTING_PROCESS, side_effect=SystemExit(LIT_REQUESTED_STOP_REFUSED)), mock.patch.object(launcher, LIT_TERMINATE_PROCESS) as terminate, mock.patch.object(subprocess, LIT_POPEN) as spawn,):
                    with self.assertRaisesRegex(SystemExit, LIT_REQUESTED_STOP_REFUSED):
                        launcher.launch_with_optional_profile(args, mock.sentinel.settings, Path(LIT_APPLICATION), ROOT, {}, [])
                    start_profile.assert_called_once_with(args, mock.sentinel.settings, ROOT, {})
                    spawn.assert_not_called()
                    terminate.assert_called_once_with(profile_process, launcher.LOGSERVER_LABEL)


class LauncherBuildBoundaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.arguments = [LIT_REPO_ROOT, str(self.root), LIT_PLATFORM, LIT_WINDOWS, LIT_ARCH, LIT_ARM64, LIT_DOMAIN, LIT_FULL, LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64, LIT_CONFIG, LIT_OPT,]
        self.patch(launcher, "repo_root", return_value=self.root)
        self.patch(launcher, LIT_HOST_PLATFORM_NAME, return_value=LIT_WINDOWS)
        self.patch(launcher, LIT_CMAKE_COMMAND, return_value=(LIT_CMAKE_EXECUTABLE,))
        self.patch(launcher, LIT_BUILD_ENVIRONMENT, return_value={})
        self.process = self.patch(subprocess, LIT_RUN, return_value=mock.Mock(returncode=0))
        self.query = self.patch(launcher, LIT_ENSURE_FILE_API_QUERY, wraps=launcher.ensure_file_api_query)
        self.profile_build = self.patch(launcher, "build_profile_targets", wraps=launcher.build_profile_targets)
        output_patch = mock.patch(LIT_BUILTINS_PRINT)
        self.output = output_patch.start()
        self.addCleanup(output_patch.stop)
        self.forbidden = [self.patch(launcher, operation, side_effect=AssertionError(LIT_FORBIDDEN_LAUNCH)) for operation in (LIT_RESOLVE_EXECUTABLE_PATH, "resolve_working_directory", LIT_START_PROFILE_SESSION, "launch_with_optional_profile",)]
        self.popen = self.patch(subprocess, LIT_POPEN, side_effect=AssertionError(LIT_FORBIDDEN_LAUNCH))

    def patch(self, owner, name, **kwargs):
        patcher = mock.patch.object(owner, name, **kwargs)
        result = patcher.start()
        self.addCleanup(patcher.stop)
        return result


    def test_relative_cold_directory_is_resolved_against_the_foreign_repository_root(self):
        relative = Path(f"{LIT_COLD_CUSTOM_BUILD}-{self.root.name}")
        custom = self.root / relative
        caller_directory = Path.cwd() / relative
        caller_existed = caller_directory.exists()
        self.assertNotEqual(Path.cwd(), self.root)

        with mock.patch.object(launcher, LIT_ENSURE_FILE_API_QUERY) as query:
            self.assertEqual(0, launcher.main([LIT_BUILD, LIT_LIBRARY_TARGET, *self.arguments, LIT_BUILD_DIR, str(relative),]))

        query.assert_called_once_with(custom)
        commands = [call.args[0] for call in self.process.call_args_list]
        self.assertEqual(2, len(commands))
        self.assertEqual(str(custom), commands[0][commands[0].index("-B") + 1])
        self.assertEqual(str(custom), commands[1][commands[1].index(LIT_BUILD_2) + 1])
        self.assertFalse(custom.exists())
        self.assertEqual(caller_existed, caller_directory.exists())
        self.popen.assert_not_called()

    def test_project_build_only_does_not_require_an_application_executable(self):
        specification = importlib.util.spec_from_file_location("nwb_test_testbed_launcher", ROOT / LIT_COOLSTUFF / "Testbed" / LIT_LAUNCH_PY,)
        project = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(project)

        self.assertEqual(0, project.main([*self.arguments, LIT_BUILD_ONLY]))

        self.assertEqual(2, self.process.call_count)
        for operation in self.forbidden:
            operation.assert_not_called()
        self.popen.assert_not_called()

    def test_build_dry_run_leaves_cold_custom_directories_and_processes_untouched(self):
        custom = self.root / LIT_COLD_CUSTOM_BUILD
        invocations = ([LIT_BUILD, LIT_LIBRARY_TARGET, LIT_AGGREGATE_TARGET], [LIT_RUN, LIT_TESTBED, LIT_BUILD_ONLY, LIT_WITH_PROFILE],)
        for invocation in invocations:
            with self.subTest(command=invocation):
                self.output.reset_mock()
                before = tuple(self.root.rglob("*"))
                self.assertEqual(0, launcher.main([*invocation, *self.arguments, LIT_BUILD_DIR, str(custom), LIT_DRY_RUN,]))
                self.assertEqual(before, tuple(self.root.rglob("*")))
                self.assertFalse(custom.exists())
                self.query.assert_not_called()
                self.process.assert_not_called()
                self.popen.assert_not_called()

    def test_configuration_failure_prevents_build_and_launch(self):
        invocations = ([LIT_BUILD, LIT_LIBRARY_TARGET], [LIT_RUN, LIT_TESTBED], [LIT_RUN, LIT_TESTBED, LIT_BUILD_ONLY, LIT_WITH_PROFILE],)
        for invocation in invocations:
            with self.subTest(command=invocation):
                self.process.reset_mock()
                self.profile_build.reset_mock()
                self.process.side_effect = [mock.Mock(returncode=23)]
                with self.assertRaises(SystemExit) as failure:
                    launcher.main([*invocation, *self.arguments])
                self.assertEqual(23, failure.exception.code)
                self.assertEqual(1, self.process.call_count)
                self.assertNotIn(LIT_BUILD_2, self.process.call_args.args[0])
                self.profile_build.assert_not_called()
                self.popen.assert_not_called()

    def test_target_build_failure_prevents_profile_build_and_launch(self):
        invocations = ([LIT_BUILD, LIT_LIBRARY_TARGET, LIT_AGGREGATE_TARGET], [LIT_RUN, LIT_TESTBED], [LIT_RUN, LIT_TESTBED, LIT_BUILD_ONLY, LIT_WITH_PROFILE],)
        for invocation in invocations:
            with self.subTest(command=invocation):
                self.process.reset_mock()
                self.profile_build.reset_mock()
                self.process.side_effect = [mock.Mock(returncode=0), mock.Mock(returncode=37)]
                with self.assertRaises(SystemExit) as failure:
                    launcher.main([*invocation, *self.arguments])
                self.assertEqual(37, failure.exception.code)
                self.assertEqual(2, self.process.call_count)
                self.assertIn(LIT_BUILD_2, self.process.call_args.args[0])
                self.profile_build.assert_not_called()
                self.popen.assert_not_called()

    def test_conflicting_or_malformed_build_arguments_fail_before_side_effects(self):
        invocations = ([LIT_BUILD], [LIT_BUILD, LIT_LIBRARY_TARGET, LIT_SKIP_BUILD], [LIT_BUILD, LIT_LIBRARY_TARGET, LIT_WITH_PROFILE],
            [LIT_BUILD, LIT_LIBRARY_TARGET, LIT_EMPTY, LIT_APPLICATION_ARGUMENT], [LIT_RUN, LIT_TESTBED, LIT_BUILD_ONLY, LIT_SKIP_BUILD], [LIT_RUN, LIT_TESTBED, LIT_BUILD_ONLY, LIT_EMPTY, LIT_APPLICATION_ARGUMENT],)
        with mock.patch.object(sys, LIT_STDERR):
            for invocation in invocations:
                with self.subTest(command=invocation):
                    before = tuple(self.root.rglob("*"))
                    with self.assertRaises(SystemExit) as failure:
                        launcher.main([invocation[0], *self.arguments, *invocation[1:]])
                    self.assertNotEqual(0, failure.exception.code)
                    self.assertEqual(before, tuple(self.root.rglob("*")))
                    self.query.assert_not_called()
                    self.process.assert_not_called()
                    self.popen.assert_not_called()

    def test_build_help_does_not_create_a_cold_build_tree(self):
        with self.assertRaises(SystemExit) as result:
            launcher.main([LIT_BUILD, LIT_HELP])
        self.assertEqual(0, result.exception.code)
        self.assertEqual((), tuple(self.root.rglob("*")))
        self.query.assert_not_called()
        self.process.assert_not_called()
        self.popen.assert_not_called()

    def test_reserved_build_command_cannot_be_shadowed_by_a_project_leaf(self):
        category = self.root / LIT_COOLSTUFF
        leaf = category / LIT_BUILD
        leaf.mkdir(parents=True)
        (category / LIT_LAUNCH_PY).write_text("", encoding=LIT_UTF_8)
        (leaf / LIT_LAUNCH_PY).write_text("", encoding=LIT_UTF_8)

        with self.assertRaisesRegex(SystemExit, "conflicts with a built-in launcher command"):
            launcher.main([LIT_BUILD, LIT_LIBRARY_TARGET, *self.arguments])

        self.query.assert_not_called()
        self.process.assert_not_called()
        self.popen.assert_not_called()


class LauncherDryRunDomainTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.custom = self.root / LIT_COLD_CUSTOM_BUILD
        self.arguments = [LIT_RUN, LIT_TESTBED, LIT_REPO_ROOT, str(self.root), LIT_PLATFORM, LIT_WINDOWS, LIT_ARCH, LIT_ARM64, LIT_CONFIG, LIT_OPT, LIT_BUILD_DIR, str(self.custom), LIT_DRY_RUN,]
        output_patch = mock.patch(LIT_BUILTINS_PRINT)
        self.output = output_patch.start()
        self.addCleanup(output_patch.stop)
        for owner, operation, options in ((launcher, LIT_CMAKE_COMMAND, {LIT_RETURN_VALUE: (LIT_CMAKE_EXECUTABLE,)}), (launcher, LIT_HOST_PLATFORM_NAME, {LIT_RETURN_VALUE: LIT_WINDOWS}), (launcher, "host_arch_name", {LIT_RETURN_VALUE: LIT_ARM64}),
            (launcher, LIT_ENSURE_FILE_API_QUERY, {"wraps": launcher.ensure_file_api_query}), (subprocess, LIT_RUN, {LIT_SIDE_EFFECT: AssertionError(LIT_FORBIDDEN_LAUNCH)}), (subprocess, LIT_POPEN, {LIT_SIDE_EFFECT: AssertionError(LIT_FORBIDDEN_LAUNCH)}),):
            patcher = mock.patch.object(owner, operation, **options)
            result = patcher.start()
            self.addCleanup(patcher.stop)
            if LIT_RETURN_VALUE not in options:
                self.addCleanup(result.assert_not_called)

    def assert_preview_uses_domain(self, expected_domain, extra_arguments):
        self.output.reset_mock()
        args = launcher.make_parser({}).parse_args([*self.arguments, *extra_arguments])
        args.application_args = []
        before = tuple((path, path.read_bytes() if path.is_file() else None) for path in sorted(self.root.rglob("*")))
        settings = launcher.resolve_launch_settings(args, LIT_FULL)
        self.assertEqual(expected_domain, settings.domain)
        self.assertEqual(expected_domain, launcher.refresh_launch_settings(settings, args.domain).domain)

        self.assertEqual(0, launcher.run_target_command(args))

        output = self.root / LIT_EXEC_OUTPUT_ROOT / LIT_WINDOWS / LIT_ARM64
        if expected_domain != LIT_ENGINE:
            output /= expected_domain
        executable = output / LIT_OPT / "testbed.exe"
        printed = [str(call.args[0]) for call in self.output.call_args_list]
        self.assertIn("+ " + launcher.format_command([str(executable)]), printed)
        self.assertIn("  cwd: " + str(output), printed)
        after = tuple((path, path.read_bytes() if path.is_file() else None) for path in sorted(self.root.rglob("*")))
        self.assertEqual(before, after)

    def test_cold_custom_directory_preview_uses_selected_preset_instead_of_directory_name(self):
        cases = ((LIT_FULL, []), (LIT_FULL, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64]), (LIT_ENGINE, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ENGINE_ARM64]), (LIT_TESTBED, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_TESTBED_ARM64]),)
        for expected_domain, arguments in cases:
            with self.subTest(domain=expected_domain, arguments=arguments):
                self.assert_preview_uses_domain(expected_domain, arguments)
                self.assertFalse(self.custom.exists())

    def test_cache_and_explicit_domain_override_the_custom_directory_preset_fallback(self):
        self.custom.mkdir()
        (self.custom / LIT_CMAKECACHE_TXT).write_text("NWB_OUTPUT_DOMAIN:STRING=testbed\n", encoding=LIT_UTF_8)
        cases = ((LIT_TESTBED, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64]), (LIT_ENGINE, [LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64, LIT_DOMAIN, LIT_ENGINE]),)
        for expected_domain, arguments in cases:
            with self.subTest(domain=expected_domain):
                self.assert_preview_uses_domain(expected_domain, arguments)


class LauncherExecutableMetadataTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.settings = launcher.LaunchSettings(root=self.root, platform_name=LIT_WINDOWS, arch=LIT_ARM64, domain=LIT_FULL, config=LIT_OPT, configure_preset=LIT_WINDOWS_CLANG_ARM64, build_dir=self.root / LIT_CMAKE / LIT_BUILD, cmake=(LIT_CMAKE_EXECUTABLE,),)
        self.reply = self.settings.build_dir / launcher.FILE_API_DIR_CMAKE / launcher.FILE_API_DIR_API / launcher.FILE_API_DIR_V1 / launcher.FILE_API_DIR_REPLY
        self.reply.mkdir(parents=True)
        self.actual = self.settings.build_dir / LIT_ACTUAL_EXECUTABLE

    def write_target_metadata(self, target_type, artifacts):
        documents = { LIT_METADATA_INDEX: { launcher.FILE_API_OBJECTS_KEY: [{ launcher.FILE_API_KIND_KEY: launcher.FILE_API_KIND_CODEMODEL, launcher.FILE_API_VERSION_KEY: {launcher.FILE_API_VERSION_MAJOR: launcher.FILE_API_CODEMODEL_VERSION},
                    launcher.FILE_API_JSON_FILE_KEY: LIT_METADATA_CODEMODEL, }], }, LIT_METADATA_CODEMODEL: { launcher.FILE_API_CONFIGURATIONS_KEY: [{ launcher.FILE_API_NAME_KEY: self.settings.config, launcher.FILE_API_TARGETS_KEY: [{
                        launcher.FILE_API_NAME_KEY: LIT_TESTBED, launcher.FILE_API_JSON_FILE_KEY: LIT_METADATA_TARGET, }], }], }, LIT_METADATA_TARGET: { launcher.FILE_API_NAME_KEY: LIT_TESTBED, launcher.FILE_API_TYPE_KEY: target_type,
                launcher.FILE_API_ARTIFACTS_KEY: [{launcher.FILE_API_PATH_KEY: str(path)} for path in artifacts], }, }
        for name, document in documents.items():
            (self.reply / name).write_text(json.dumps(document), encoding=LIT_UTF_8)

    def test_missing_metadata_rejects_guess_but_keeps_explicit_and_preview_paths(self):
        predicted = launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, True)
        predicted.parent.mkdir(parents=True)
        predicted.touch()
        with self.assertRaisesRegex(SystemExit, LIT_CMAKE_FILE_API_METADATA_IS_REQUIRED):
            launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, False)
        explicit = Path(LIT_EXPLICIT_EXECUTABLE)
        self.assertEqual(self.root / explicit, launcher.resolve_executable_path(self.settings, LIT_TESTBED, explicit, None, False))
        self.assertEqual(predicted, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, True))

        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [self.actual])
        self.assertEqual(self.actual, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, False))

    def test_unmatched_single_configuration_cannot_supply_the_selected_executable(self):
        codemodel_path = self.reply / LIT_METADATA_CODEMODEL
        for other_config in (LIT_DBG, LIT_RELEASE, launcher.EMPTY_STRING):
            with self.subTest(configuration=other_config):
                self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [self.actual])
                codemodel = json.loads(codemodel_path.read_text(encoding=LIT_UTF_8))
                codemodel[launcher.FILE_API_CONFIGURATIONS_KEY][0][launcher.FILE_API_NAME_KEY] = other_config
                codemodel_path.write_text(json.dumps(codemodel), encoding=LIT_UTF_8)
                with self.assertRaisesRegex(SystemExit, LIT_CMAKE_FILE_API_METADATA_IS_REQUIRED):
                    launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, LIT_PREVIEW_EXECUTABLE, False)

        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [self.actual])
        self.assertEqual(self.actual, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False))

    def test_executable_metadata_without_artifact_or_with_library_type_is_rejected(self):
        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [])
        with self.assertRaisesRegex(SystemExit, "has no executable artifact"):
            launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False)
        self.write_target_metadata(LIT_METADATA_LIBRARY, [self.actual])
        with self.assertRaisesRegex(SystemExit, "CMake target is not executable"):
            launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False)
        self.write_target_metadata(launcher.FILE_API_TARGET_EXECUTABLE, [Path(LIT_ACTUAL_EXECUTABLE)])
        self.assertEqual(self.actual, launcher.resolve_executable_path(self.settings, LIT_TESTBED, None, None, False))


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


class PipelineLauncherTests(unittest.TestCase):
    def setUp(self):
        specification = importlib.util.spec_from_file_location("nwb_test_pipeline_launcher", ROOT / LIT_PIPELINE / LIT_LAUNCH_PY)
        self.assertIsNotNone(specification)
        self.assertIsNotNone(specification.loader)
        self.pipeline = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(self.pipeline)
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.settings = launcher.LaunchSettings(root=self.root, platform_name=LIT_WINDOWS, arch=LIT_ARM64, domain=LIT_FULL, config=LIT_OPT, configure_preset=LIT_WINDOWS_CLANG_ARM64, build_dir=self.root / LIT_CMAKE / LIT_BUILD / LIT_WINDOWS_CLANG_ARM64,
            cmake=(LIT_CMAKE_EXECUTABLE,),)
        self.environment = {"NWB_TEST_ENVIRONMENT": LIT_PIPELINE}
        self.asset_roots = [self.root / "first assets", self.root / "second assets"]
        for index, asset_root in enumerate(self.asset_roots):
            asset_root.mkdir()
            (asset_root / f"asset-{index}.nwb").write_text("", encoding=LIT_UTF_8)
        self.cache = self.root / "asset cache"
        self.output = self.root / LIT_RUNTIME_RESOURCES
        self.arguments = [LIT_REPO_ROOT, str(self.root), LIT_PLATFORM, LIT_WINDOWS, LIT_ARCH, LIT_ARM64, LIT_CONFIG, LIT_OPT, LIT_ASSET_ROOT, *(str(path) for path in self.asset_roots),
            LIT_OUTPUT_DIRECTORY, str(self.output), LIT_CACHE_DIRECTORY, str(self.cache),]
        self.tool_paths = { "nwb_dependency_computer": self.root / LIT_CUSTOM_ARTIFACTS / LIT_DEPENDENCY_COMPUTER_EXE, LIT_NWB_ASSET_BUILDER: self.root / LIT_CUSTOM_ARTIFACTS / LIT_ASSET_BUILDER_EXE,
            "nwb_asset_gatherer": self.root / LIT_CUSTOM_ARTIFACTS / LIT_ASSET_GATHERER_EXE, }
        for path in self.tool_paths.values():
            path.parent.mkdir(exist_ok=True)
            path.touch()
        self.settings_resolver = self.patch(launcher, "resolve_launch_settings", return_value=self.settings)
        self.refresh = self.patch(launcher, "refresh_launch_settings", return_value=self.settings)
        self.patch(launcher, LIT_BUILD_ENVIRONMENT, return_value=self.environment)
        self.patch(launcher, LIT_HOST_PLATFORM_NAME, return_value=LIT_WINDOWS)
        self.process = self.patch(subprocess, LIT_RUN, return_value=mock.Mock(returncode=0))
        self.stage = self.patch(self.pipeline, "run_stage")

    def patch(self, owner, name, **kwargs):
        patcher = mock.patch.object(owner, name, **kwargs)
        result = patcher.start()
        self.addCleanup(patcher.stop)
        return result

    def prepare_pipeline_build(self):
        configure = self.patch(launcher, "maybe_configure")
        build = self.patch(launcher, "build_target")
        resolve = self.patch(launcher, LIT_RESOLVE_EXECUTABLE_PATH, side_effect=lambda settings, target, override, base_name, dry_run: override or self.tool_paths[target],)
        return configure, build, resolve


    def test_build_failure_propagates_without_resolving_or_launching_tools(self):
        _, build, resolve = self.prepare_pipeline_build()
        build.side_effect = SystemExit(21)

        with self.assertRaises(SystemExit) as failure:
            self.pipeline.main(self.arguments)

        self.assertEqual(21, failure.exception.code)
        resolve.assert_not_called()
        self.stage.assert_not_called()
        self.process.assert_not_called()
        self.assertFalse(self.cache.exists())


    def test_pipeline_dry_run_has_no_subprocess_or_output_side_effects(self):
        with mock.patch(LIT_BUILTINS_PRINT), mock.patch.object(subprocess, LIT_POPEN) as popen:
            self.assertEqual(0, self.pipeline.main([*self.arguments, LIT_DRY_RUN]))

        self.process.assert_not_called()
        self.stage.assert_not_called()
        popen.assert_not_called()
        self.assertFalse(self.settings.build_dir.exists())
        self.assertFalse(self.cache.exists())
        self.assertFalse(self.output.exists())

    def test_help_skips_configuration_build_and_target_resolution(self):
        configure, build, resolve = self.prepare_pipeline_build()
        with mock.patch(LIT_BUILTINS_PRINT), self.assertRaises(SystemExit) as result:
            self.pipeline.main([LIT_HELP])

        self.assertEqual(0, result.exception.code)
        self.settings_resolver.assert_not_called()
        configure.assert_not_called()
        build.assert_not_called()
        resolve.assert_not_called()
        self.stage.assert_not_called()
        self.process.assert_not_called()

    def test_stage_failure_reports_failure_and_prevents_later_stages(self):
        self.prepare_pipeline_build()
        for failed_index, executable in enumerate(self.tool_paths.values()):
            with self.subTest(stage=executable.name):
                self.stage.reset_mock()
                self.stage.side_effect = [None] * failed_index + [subprocess.CalledProcessError(23, [str(executable)])]
                with mock.patch(LIT_BUILTINS_PRINT):
                    self.assertEqual(1, self.pipeline.main(self.arguments))
                self.assertEqual(failed_index + 1, self.stage.call_count)


if __name__ == LIT_MAIN:
    unittest.main()
