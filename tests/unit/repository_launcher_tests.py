import argparse
import ctypes
import os
import platform
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from launcher import constants as launcher
from launcher.build import BuildController
from launcher.cli import LauncherCli
from launcher.cmake_settings import CmakeSettings
from launcher.cmake_file_api import CmakeFileApi
from launcher.host import HostProbe
from launcher.process import ProcessLauncher
from launcher.profile import ProfileSessionController
from repository_launcher_windows_process_tests import LauncherWindowsProcessTests
from repository_name_symbol_capture_tests import NameSymbolCaptureBoundaryTests
from repository_launcher_discovery_tests import LauncherDiscoveryFailureTests, LauncherDiscoveryTests


LIT_NAN = "nan"
LIT_INF = "inf"
LIT_INF_2 = "-inf"
LIT_VALIDATE_LAUNCH_PATHS = "validate_launch_paths"
LIT_STOP_EXISTING_PROCESS = "stop_existing_process"
LIT_ABSENT_RUNTIME = "absent_runtime"
LIT_RESOLVE_PROFILE_LOG_PORT = "resolve_profile_log_port"
LIT_RESOLVE_EXECUTABLE_PATH = "resolve_executable_path"
LIT_WINDOWS = "windows"
LIT_WINDOWS_2 = "Windows"
LIT_AMD64 = "AMD64"
LIT_ARM64 = "arm64"
LIT_ARM64_2 = "ARM64"
LIT_SYSTEM = "system"
LIT_MACHINE = "machine"
LIT_QUERY_WINDOWS_NATIVE_MACHINE_NAME = "query_windows_native_machine_name"
LIT_REPO = "repo"
LIT_FULL = "full"
LIT_DBG = "dbg"
LIT_TESTBED = "testbed"
LIT_UTF_8 = "utf-8"
LIT_RUN = "run"
LIT_POPEN = "Popen"
LIT_MAIN = "__main__"
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


class LauncherPlatformTests(unittest.TestCase):
    def test_host_architecture_uses_native_windows_machine_under_emulation(self):
        with (mock.patch.object(platform, LIT_SYSTEM, return_value=LIT_WINDOWS_2), mock.patch.object(platform, LIT_MACHINE, return_value=LIT_AMD64), mock.patch.object(HostProbe, LIT_QUERY_WINDOWS_NATIVE_MACHINE_NAME, return_value=LIT_ARM64_2),
            mock.patch.dict(os.environ, {}, clear=True),):
            self.assertEqual(LIT_ARM64, HostProbe.host_arch_name())

    def test_missing_native_windows_api_rejects_environment_and_process_guesses(self):
        with (mock.patch.object(platform, LIT_SYSTEM, return_value=LIT_WINDOWS_2), mock.patch.object(platform, LIT_MACHINE, return_value=LIT_AMD64) as machine, mock.patch.object(ctypes, LIT_WINDOWS_DLL, return_value=mock.Mock(spec=()), create=True),
            mock.patch.dict(os.environ, {LIT_WINDOWS_ENV_ARCH6432: LIT_ARM64_2}, clear=True),):
            with self.assertRaisesRegex(SystemExit, launcher.WINDOWS_WOW64_PROC2):
                HostProbe.host_arch_name()
            machine.assert_not_called()

    def test_failed_native_windows_query_rejects_guesses_and_recovers_after_success(self):
        kernel32 = mock.Mock()
        kernel32.GetCurrentProcess.return_value = 1
        query = getattr(kernel32, launcher.WINDOWS_WOW64_PROC2)
        query.return_value = 0
        with (mock.patch.object(platform, LIT_SYSTEM, return_value=LIT_WINDOWS_2), mock.patch.object(platform, LIT_MACHINE, return_value=LIT_AMD64) as machine, mock.patch.object(ctypes, LIT_WINDOWS_DLL, return_value=kernel32, create=True),
            mock.patch.object(ctypes, LIT_WINDOWS_LAST_ERROR, return_value=87, create=True), mock.patch.dict(os.environ, {LIT_WINDOWS_ENV_ARCH6432: LIT_AMD64}, clear=True),):
            with self.assertRaisesRegex(SystemExit, "Windows error 87"):
                HostProbe.host_arch_name()

            def report_arm64(process, process_machine, native_machine):
                native_machine._obj.value = launcher.WINDOWS_IMAGE_FILE_MACHINE_ARM64
                return 1

            query.side_effect = report_arm64
            self.assertEqual(LIT_ARM64, HostProbe.host_arch_name())
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
                HostProbe.query_windows_native_machine_name()

    def test_explicit_architecture_must_match_configure_preset(self):
        args = argparse.Namespace(repo_root=Path(os.sep) / LIT_REPO, platform=LIT_WINDOWS, arch=LIT_ARM64, domain=None, configure_preset="windows-clang-x64", build_dir=None, config=LIT_DBG, cmake=None,)
        with self.assertRaisesRegex(SystemExit, "conflicts with configure preset"):
            CmakeSettings.resolve_launch_settings(args, LIT_FULL)


    def test_owned_child_output_reaches_redirected_parent_streams(self):
        args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=False, run_seconds=None,)

        with (tempfile.TemporaryDirectory() as temp_dir, tempfile.TemporaryFile(mode=LIT_READ_WRITE_TEXT, encoding=LIT_UTF_8) as stdout, tempfile.TemporaryFile(mode=LIT_READ_WRITE_TEXT, encoding=LIT_UTF_8) as stderr,):
            with (mock.patch.object(sys, LIT_STDOUT, stdout), mock.patch.object(sys, LIT_STDERR, stderr),):
                exit_code = ProcessLauncher.launch_process(args, Path(sys.executable), Path(temp_dir), os.environ.copy(), (LIT_PYTHON_COMMAND, LIT_CHILD_STREAMS_PROGRAM),)

            stdout.seek(0)
            stderr.seek(0)
            stdout_lines = stdout.read().splitlines()
            stderr_lines = stderr.read().splitlines()

        self.assertEqual(23, exit_code)
        self.assertIn(LIT_CHILD_STDOUT, stdout_lines)
        self.assertNotIn(LIT_CHILD_STDERR, stdout_lines)
        self.assertEqual([LIT_CHILD_STDERR], stderr_lines)


class LauncherCmakeReadFailureTests(unittest.TestCase):
    def test_unreadable_cache_does_not_look_missing(self):
        with mock.patch.object(Path, "open", side_effect=PermissionError("cache denied")):
            with self.assertRaisesRegex(PermissionError, "cache denied"):
                CmakeSettings.read_cmake_cache_value(Path("build"), "NWB_OUTPUT_DOMAIN")
        with mock.patch.object(Path, "open", side_effect=FileNotFoundError):
            self.assertIsNone(CmakeSettings.read_cmake_cache_value(Path("build"), "NWB_OUTPUT_DOMAIN"))

    def test_unreadable_file_api_directory_does_not_look_missing(self):
        with mock.patch("launcher.cmake_file_api.os.scandir", side_effect=PermissionError("reply denied")):
            with self.assertRaisesRegex(PermissionError, "reply denied"):
                CmakeFileApi.latest_file_api_index(Path("build"))
        with mock.patch("launcher.cmake_file_api.os.scandir", side_effect=FileNotFoundError):
            self.assertIsNone(CmakeFileApi.latest_file_api_index(Path("build")))


class LauncherDurationBoundaryTests(unittest.TestCase):
    def test_detach_cannot_silently_disable_a_run_deadline(self):
        with mock.patch("sys.stderr"), self.assertRaises(SystemExit):
            LauncherCli.make_parser({}).parse_args([LIT_RUN, LIT_TESTBED, "--detach", "--run-seconds", "1"])
        args = argparse.Namespace(run_seconds=1.0, kill_existing=True, dry_run=False, detach=True, gpudbg=False)
        with mock.patch.object(ProcessLauncher, LIT_VALIDATE_LAUNCH_PATHS) as validate, mock.patch.object(ProcessLauncher, LIT_STOP_EXISTING_PROCESS) as stop, mock.patch.object(subprocess, LIT_POPEN) as spawn:
            with self.assertRaisesRegex(SystemExit, "--detach cannot be combined"):
                ProcessLauncher.launch_process(args, Path("absent_application"), Path(LIT_ABSENT_RUNTIME), {}, ())
            validate.assert_not_called()
            stop.assert_not_called()
            spawn.assert_not_called()

    def test_cli_rejects_nonfinite_and_nonpositive_durations(self):
        for option in ("--run-seconds", "--profile-logserver-timeout"):
            for value in (LIT_NAN, LIT_INF, LIT_INF_2, "0", "-1"):
                with self.subTest(option=option, value=value), mock.patch("sys.stderr"), self.assertRaises(SystemExit):
                    LauncherCli.make_parser({}).parse_args([LIT_RUN, LIT_TESTBED, f"{option}={value}"])

    def test_direct_invalid_run_duration_cannot_validate_stop_or_spawn(self):
        for duration in (float(LIT_NAN), float(LIT_INF), float(LIT_INF_2), 0.0, -1.0):
            args = argparse.Namespace(run_seconds=duration, kill_existing=True, dry_run=False, detach=False, gpudbg=False)
            with self.subTest(duration=duration), mock.patch.object(ProcessLauncher, LIT_VALIDATE_LAUNCH_PATHS) as validate, mock.patch.object(ProcessLauncher, LIT_STOP_EXISTING_PROCESS) as stop, mock.patch.object(subprocess, LIT_POPEN) as spawn:
                with self.assertRaises(SystemExit):
                    ProcessLauncher.launch_process(args, Path("absent_application"), Path(LIT_ABSENT_RUNTIME), {}, ())
                validate.assert_not_called()
                stop.assert_not_called()
                spawn.assert_not_called()

    def test_direct_invalid_profile_timeout_cannot_resolve_or_spawn_server(self):
        for duration in (float(LIT_NAN), float(LIT_INF), float(LIT_INF_2), 0.0, -1.0):
            args = argparse.Namespace(with_profile=True, profile_logserver_timeout=duration)
            with self.subTest(duration=duration), mock.patch.object(ProfileSessionController, LIT_RESOLVE_PROFILE_LOG_PORT) as port, mock.patch.object(BuildController, LIT_RESOLVE_EXECUTABLE_PATH) as resolve, mock.patch.object(subprocess, LIT_POPEN) as spawn:
                with self.assertRaises(SystemExit):
                    ProfileSessionController.start_profile_session(args, mock.sentinel.settings, Path(LIT_ABSENT_RUNTIME), {})
                port.assert_not_called()
                resolve.assert_not_called()
                spawn.assert_not_called()


if __name__ == LIT_MAIN:
    unittest.main()
