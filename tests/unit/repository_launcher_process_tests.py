"""Repository launcher process-stop and build-boundary behavior."""


import argparse
import importlib.util
import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from launcher import constants as launcher
from launcher import readiness
from launcher.build import BuildController
from launcher.cli import LauncherCli
from launcher.cmake_settings import CmakeSettings
from launcher.host import HostProbe
from launcher.models import ProfileSession
from launcher.process import ProcessLauncher
from launcher.profile import ProfileSessionController
from repository_launcher_test_support import FakeSpawnedProcess


LIT_TERMINATE_PROCESS = "terminate_process"
LIT_VALIDATE_LAUNCH_PATHS = "validate_launch_paths"
LIT_STOP_EXISTING_PROCESS = "stop_existing_process"
LIT_RESOLVE_PROFILE_LOG_PORT = "resolve_profile_log_port"
LIT_RESOLVE_EXECUTABLE_PATH = "resolve_executable_path"
LIT_SERVER = "server"
LIT_LAUNCHED = "launched "
LIT_HTTP_LOCALHOST = "http://localhost"
LIT_APPLICATION = "application"
LIT_START_PROFILE_SESSION = "start_profile_session"
LIT_REQUESTED_STOP_REFUSED = "requested stop refused"
LIT_CMAKE_COMMAND = "cmake_command"
LIT_BUILD_ENVIRONMENT = "build_environment"
LIT_ENSURE_FILE_API_QUERY = "ensure_file_api_query"
LIT_BUILD_2 = "--build"
LIT_APPLICATION_ARGUMENT = "application argument"
LIT_WAIT = "wait"
LIT_TERMINATE = "terminate"
LIT_WINDOWS = "windows"
LIT_ARM64 = "arm64"
LIT_CMAKE_EXECUTABLE = "cmake"
LIT_BUILD = "build"
LIT_FULL = "full"
LIT_WINDOWS_CLANG_ARM64 = "windows-clang-arm64"
LIT_TESTBED = "testbed"
LIT_UTF_8 = "utf-8"
LIT_RUN = "run"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_LAUNCH_PY = "launch.py"
LIT_COOLSTUFF = "CoolStuff"
LIT_DRY_RUN = "--dry-run"
LIT_CONFIG = "--config"
LIT_EMPTY = "--"
LIT_OPT = "opt"
LIT_REPO_ROOT = "--repo-root"
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
LIT_STDERR = "stderr"


class LauncherProcessStopTests(unittest.TestCase):
    def test_profile_spawn_diagnostic_failure_reaps_unpublished_server(self):
        events = []
        process = FakeSpawnedProcess(701, 0, events)
        failure = BrokenPipeError("closed launcher output")
        args = argparse.Namespace(with_profile=True, profile_logserver_timeout=1.0, profile_logserver_target=LIT_SERVER, profile_logserver_executable=None, profile_logserver_name=None, profile_logserver_args=[], dry_run=False)

        def print_status(message, **_options):
            if str(message).startswith(LIT_LAUNCHED):
                raise failure

        with (mock.patch.object(ProfileSessionController, LIT_RESOLVE_PROFILE_LOG_PORT, return_value=12345), mock.patch.object(BuildController, LIT_RESOLVE_EXECUTABLE_PATH, return_value=Path(LIT_SERVER)), mock.patch.object(ProcessLauncher, LIT_VALIDATE_LAUNCH_PATHS),
            mock.patch.object(ProfileSessionController, "wait_for_tcp_port") as ready, mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch(LIT_BUILTINS_PRINT, side_effect=print_status)):
            with self.assertRaises(BrokenPipeError) as result:
                ProfileSessionController.start_profile_session(args, mock.sentinel.settings, ROOT, {})

        self.assertIs(failure, result.exception)
        self.assertEqual([(LIT_TERMINATE, 701), (LIT_WAIT, launcher.PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS)], events)
        self.assertEqual(0, process.returncode)
        ready.assert_not_called()

    def test_cleanup_failure_does_not_replace_active_failure(self):
        process = mock.Mock(pid=702)
        process.poll.return_value = None
        process.terminate.side_effect = OSError("terminate denied")
        output = io.StringIO()
        with redirect_stderr(output):
            with self.assertRaisesRegex(ValueError, "startup failed"):
                try:
                    raise ValueError("startup failed")
                finally:
                    ProcessLauncher.terminate_process(process, LIT_SERVER)
        self.assertIn("terminate denied", output.getvalue())

    def test_cleanup_poll_failure_does_not_replace_active_failure(self):
        process = mock.Mock(pid=702)
        process.poll.side_effect = OSError("poll denied")
        output = io.StringIO()
        with redirect_stderr(output):
            with self.assertRaisesRegex(ValueError, "startup failed"):
                try:
                    raise ValueError("startup failed")
                finally:
                    ProcessLauncher.terminate_process(process, LIT_SERVER)
        self.assertIn("poll denied", output.getvalue())

    def test_readiness_rejects_an_unrelated_listener(self):
        process = mock.Mock()
        process.poll.return_value = None
        connection = mock.MagicMock()
        connection.__enter__.return_value = connection
        connection.recv.return_value = b"HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nother-token"
        with mock.patch.object(readiness.socket, "create_connection", return_value=connection):
            with self.assertRaisesRegex(readiness.ReadinessError, "different readiness identity"):
                readiness.wait_for_logserver(12345, "expected-token", 1.0, process)
        connection.__exit__.assert_called_once()

    def test_readiness_rejects_child_exit_after_response(self):
        process = mock.Mock(returncode=7)
        process.poll.side_effect = [None, None, None, None, 7]
        connection = mock.MagicMock()
        connection.__enter__.return_value = connection
        connection.recv.return_value = b"HTTP/1.1 200 OK\r\nContent-Length: 14\r\n\r\nexpected-token"
        with mock.patch.object(readiness.socket, "create_connection", return_value=connection):
            with self.assertRaisesRegex(readiness.ReadinessError, "exited during readiness"):
                readiness.wait_for_logserver(12345, "expected-token", 1.0, process)
        connection.__exit__.assert_called_once()

    def test_readiness_keeps_connection_after_a_receive_timeout(self):
        process = mock.Mock()
        process.poll.return_value = None
        connection = mock.MagicMock()
        connection.__enter__.return_value = connection
        connection.recv.side_effect = [readiness.socket.timeout(), b"HTTP/1.1 200 OK\r\nContent-Length: 14\r\n\r\nexpected-token"]
        with mock.patch.object(readiness.socket, "create_connection", return_value=connection) as connect:
            readiness.wait_for_logserver(12345, "expected-token", 1.0, process)
        connect.assert_called_once()

    def test_readiness_deadline_limits_a_slow_trickling_response(self):
        process = mock.Mock()
        process.poll.return_value = None
        connection = mock.MagicMock()
        connection.__enter__.return_value = connection
        def slow_byte(_size):
            readiness.time.sleep(0.03)
            return b"x"
        connection.recv.side_effect = slow_byte
        start = readiness.time.monotonic()
        with mock.patch.object(readiness.socket, "create_connection", return_value=connection):
            with self.assertRaisesRegex(readiness.ReadinessError, "timed out"):
                readiness.wait_for_logserver(12345, "expected-token", 0.1, process)
        self.assertLess(readiness.time.monotonic() - start, 1.0)

    def test_cleanup_diagnostic_failure_still_forces_and_reaps_process(self):
        process = mock.Mock(pid=702)
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired(LIT_SERVER, 1.0), 9]
        failure = BrokenPipeError("closed cleanup output")

        with mock.patch(LIT_BUILTINS_PRINT, side_effect=failure), self.assertRaises(BrokenPipeError) as result:
            ProcessLauncher.terminate_process(process, LIT_SERVER, 1.0)

        self.assertIs(failure, result.exception)
        self.assertEqual([mock.call.poll(), mock.call.terminate(), mock.call.wait(timeout=1.0), mock.call.kill(), mock.call.wait()], process.mock_calls)

    def test_application_spawn_diagnostic_failure_reaps_application_and_server(self):
        for detach in (False, True):
            with self.subTest(detach=detach):
                events = []
                process = FakeSpawnedProcess(703, 0, events)
                server = FakeSpawnedProcess(704, 0, events)
                session = ProfileSession(12345, Path(LIT_SERVER), server)
                args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=detach, run_seconds=None, profile_log_address=LIT_HTTP_LOCALHOST)
                failure = BrokenPipeError("closed application output")

                def print_status(message, **_options):
                    if str(message).startswith(LIT_LAUNCHED):
                        raise failure

                with mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch(LIT_BUILTINS_PRINT, side_effect=print_status), self.assertRaises(BrokenPipeError) as result:
                    ProcessLauncher.launch_process(args, Path(LIT_APPLICATION), ROOT, {}, (), session, paths_validated=True)

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
                session = ProfileSession(12345, Path(LIT_SERVER), server)
                args = argparse.Namespace(kill_existing=False, dry_run=False, gpudbg=False, detach=detach, run_seconds=None, profile_log_address=LIT_HTTP_LOCALHOST)

                with mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch(LIT_BUILTINS_PRINT):
                    self.assertEqual(0, ProcessLauncher.launch_process(args, Path(LIT_APPLICATION), ROOT, {}, (), session, paths_validated=True))

                self.assertEqual(0 if detach else 1, process.wait.call_count)
                process.terminate.assert_not_called()
                process.kill.assert_not_called()
                server.terminate.assert_not_called()
                server.kill.assert_not_called()
                server.wait.assert_not_called()

    def test_linux_stop_signals_only_matching_image_and_closes_handles(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101", "102", "103", "other"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.side_effect = [mock.Mock(st_uid=1000), mock.Mock(st_uid=1000), mock.Mock(st_uid=2000)]
        fake_os.pidfd_open.side_effect = [11, 12, 13]
        target = Path("application").resolve()
        fake_os.readlink.side_effect = [str(target), str(target) + "-other"]
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = [(11, 1)]
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select):
            ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_called_once_with(11, 15)
        self.assertEqual([mock.call(12), mock.call(13), mock.call(11)], fake_os.close.call_args_list)
        self.assertEqual([mock.call(101), mock.call(102), mock.call(103)], fake_os.pidfd_open.call_args_list)

    def test_linux_inspection_failure_closes_handles_before_signalling(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101", "102"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.pidfd_open.side_effect = [11, 12]
        target = Path("application").resolve()
        fake_os.readlink.side_effect = [str(target), PermissionError("access denied")]
        fake_signal = mock.Mock(SIGTERM=15)
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal):
            with self.assertRaisesRegex(SystemExit, "access denied"):
                ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        self.assertEqual([mock.call(12), mock.call(11)], fake_os.close.call_args_list)

    def test_linux_disappearing_process_does_not_signal_reused_pid(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.pidfd_open.return_value = 11
        fake_os.readlink.side_effect = ProcessLookupError()
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = [(11, 1)]
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select):
            ProcessLauncher.stop_existing_process(Path("application"), launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)

    def test_linux_missing_image_for_live_process_fails_closed(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.pidfd_open.return_value = 11
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.readlink.side_effect = FileNotFoundError()
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = []
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select), mock.patch("builtins.open", mock.mock_open(read_data="Kthread:\t0\n")):
            with self.assertRaisesRegex(SystemExit, "cannot inspect live process"):
                ProcessLauncher.stop_existing_process(Path("application"), launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)

    def test_linux_deleted_or_ambiguous_image_rejects_launch_before_signalling(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101", "102"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.pidfd_open.side_effect = [11, 12]
        target = Path("application").resolve()
        fake_os.readlink.side_effect = [str(target), str(target) + " (deleted)"]
        fake_signal = mock.Mock(SIGTERM=15)
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal):
            with self.assertRaisesRegex(SystemExit, "deleted or ambiguously named"):
                ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        self.assertEqual([mock.call(12), mock.call(11)], fake_os.close.call_args_list)

    def test_linux_rejects_launch_if_matching_process_ignores_sigterm(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.pidfd_open.return_value = 11
        target = Path("application").resolve()
        fake_os.readlink.return_value = str(target)
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = []
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select):
            with self.assertRaisesRegex(SystemExit, "did not exit after SIGTERM"):
                ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_called_once_with(11, 15)
        fake_os.close.assert_called_once_with(11)

    def test_linux_ignores_kernel_threads_without_an_executable(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 0
        fake_os.pidfd_open.return_value = 11
        fake_os.readlink.side_effect = FileNotFoundError()
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = []
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select), mock.patch("builtins.open", mock.mock_open(read_data="Kthread:\t1\n")):
            ProcessLauncher.stop_existing_process(Path("application"), launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)

    def test_linux_older_kernel_thread_stat_without_status_flag(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 0
        fake_os.pidfd_open.return_value = 11
        fake_os.readlink.side_effect = FileNotFoundError()
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = []
        status = mock.mock_open(read_data="Name: worker\n")()
        stat = mock.mock_open(read_data="101 (worker with ) name) I 0 0 0 0 0 2097152 0")()
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select), mock.patch("builtins.open", side_effect=[status, stat]):
            ProcessLauncher.stop_existing_process(Path("application"), launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)

    def test_linux_waits_for_high_numbered_pidfd(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.pidfd_open.return_value = 2048
        target = Path("application").resolve()
        fake_os.readlink.return_value = str(target)
        fake_signal = mock.Mock(SIGTERM=15)
        fake_select = mock.Mock(POLLIN=1, POLLHUP=16, POLLERR=8, POLLNVAL=32)
        fake_select.poll.return_value.poll.return_value = [(2048, 1)]
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal), mock.patch("launcher.process.select", fake_select):
            ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_select.poll.return_value.register.assert_called_once_with(2048, 1)
        fake_os.close.assert_called_once_with(2048)

    def test_linux_reused_pid_owned_by_another_user_is_not_signalled(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.pidfd_open.return_value = 11
        fake_os.stat.return_value = mock.Mock(st_uid=2000)
        fake_signal = mock.Mock(SIGTERM=15)
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal):
            ProcessLauncher.stop_existing_process(Path("application"), launcher.PLATFORM_LINUX)
        fake_os.readlink.assert_not_called()
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)

    def test_linux_target_named_deleted_suffix_is_ambiguous(self):
        fake_os = mock.Mock()
        fake_os.listdir.return_value = ["101"]
        fake_os.getpid.return_value = 999
        fake_os.geteuid.return_value = 1000
        fake_os.stat.return_value = mock.Mock(st_uid=1000)
        fake_os.pidfd_open.return_value = 11
        target = Path("application (deleted)").resolve()
        fake_os.readlink.return_value = str(target)
        fake_signal = mock.Mock(SIGTERM=15)
        with mock.patch("launcher.process.os", fake_os), mock.patch("launcher.process.signal", fake_signal):
            with self.assertRaisesRegex(SystemExit, "ambiguously named"):
                ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)

    def test_linux_missing_pidfd_support_rejects_stop(self):
        fake_os = mock.Mock(spec=["listdir"])
        with mock.patch("launcher.process.os", fake_os):
            with self.assertRaisesRegex(SystemExit, "requires Linux pidfd support"):
                ProcessLauncher.stop_existing_process(Path(LIT_APPLICATION), launcher.PLATFORM_LINUX)

    def test_stop_failure_closes_started_profile_session_before_application_spawn(self):
        for detach in (False, True):
            with self.subTest(detach=detach):
                args = argparse.Namespace(dry_run=False, kill_existing=True, detach=detach)
                profile_process = mock.Mock()
                session = ProfileSession(12345, Path("logserver"), profile_process)
                with (mock.patch.object(ProcessLauncher, LIT_VALIDATE_LAUNCH_PATHS), mock.patch.object(ProfileSessionController, LIT_START_PROFILE_SESSION, return_value=session) as start_profile, mock.patch.object(HostProbe, LIT_HOST_PLATFORM_NAME, return_value=launcher.PLATFORM_LINUX),
                    mock.patch.object(ProcessLauncher, LIT_STOP_EXISTING_PROCESS, side_effect=SystemExit(LIT_REQUESTED_STOP_REFUSED)), mock.patch.object(ProcessLauncher, LIT_TERMINATE_PROCESS) as terminate, mock.patch.object(subprocess, LIT_POPEN) as spawn,):
                    with self.assertRaisesRegex(SystemExit, LIT_REQUESTED_STOP_REFUSED):
                        ProfileSessionController.launch_with_optional_profile(args, mock.sentinel.settings, Path(LIT_APPLICATION), ROOT, {}, [])
                    start_profile.assert_called_once_with(args, mock.sentinel.settings, ROOT, {})
                    spawn.assert_not_called()
                    terminate.assert_called_once_with(profile_process, launcher.LOGSERVER_LABEL)


class LauncherBuildBoundaryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.arguments = [LIT_REPO_ROOT, str(self.root), LIT_PLATFORM, LIT_WINDOWS, LIT_ARCH, LIT_ARM64, LIT_DOMAIN, LIT_FULL, LIT_CONFIGURE_PRESET, LIT_WINDOWS_CLANG_ARM64, LIT_CONFIG, LIT_OPT,]
        self.patch(CmakeSettings, "repo_root", return_value=self.root)
        self.patch(HostProbe, LIT_HOST_PLATFORM_NAME, return_value=LIT_WINDOWS)
        self.patch(CmakeSettings, LIT_CMAKE_COMMAND, return_value=(LIT_CMAKE_EXECUTABLE,))
        self.patch(BuildController, LIT_BUILD_ENVIRONMENT, return_value={})
        self.process = self.patch(subprocess, LIT_RUN, return_value=mock.Mock(returncode=0))
        self.query = self.patch(CmakeSettings, LIT_ENSURE_FILE_API_QUERY, wraps=CmakeSettings.ensure_file_api_query)
        self.profile_build = self.patch(BuildController, "build_profile_targets", wraps=BuildController.build_profile_targets)
        output_patch = mock.patch(LIT_BUILTINS_PRINT)
        self.output = output_patch.start()
        self.addCleanup(output_patch.stop)
        self.forbidden = [self.patch(owner, operation, side_effect=AssertionError(LIT_FORBIDDEN_LAUNCH)) for owner, operation in ((BuildController, LIT_RESOLVE_EXECUTABLE_PATH), (BuildController, "resolve_working_directory"), (ProfileSessionController, LIT_START_PROFILE_SESSION), (ProfileSessionController, "launch_with_optional_profile"))]
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

        with mock.patch.object(CmakeSettings, LIT_ENSURE_FILE_API_QUERY) as query:
            self.assertEqual(0, LauncherCli.main([LIT_BUILD, LIT_LIBRARY_TARGET, *self.arguments, LIT_BUILD_DIR, str(relative),]))

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
                self.assertEqual(0, LauncherCli.main([*invocation, *self.arguments, LIT_BUILD_DIR, str(custom), LIT_DRY_RUN,]))
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
                    LauncherCli.main([*invocation, *self.arguments])
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
                    LauncherCli.main([*invocation, *self.arguments])
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
                        LauncherCli.main([invocation[0], *self.arguments, *invocation[1:]])
                    self.assertNotEqual(0, failure.exception.code)
                    self.assertEqual(before, tuple(self.root.rglob("*")))
                    self.query.assert_not_called()
                    self.process.assert_not_called()
                    self.popen.assert_not_called()

    def test_build_help_does_not_create_a_cold_build_tree(self):
        with self.assertRaises(SystemExit) as result:
            LauncherCli.main([LIT_BUILD, LIT_HELP])
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
            LauncherCli.main([LIT_BUILD, LIT_LIBRARY_TARGET, *self.arguments])

        self.query.assert_not_called()
        self.process.assert_not_called()
        self.popen.assert_not_called()


if __name__ == LIT_MAIN:
    unittest.main()
