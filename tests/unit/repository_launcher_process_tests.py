"""Repository launcher process-stop and build-boundary behavior."""


import argparse
import io
import subprocess
import sys
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from launcher import constants as launcher
from launcher import readiness
from launcher.build import BuildController
from launcher.host import HostProbe
from launcher.models import ProfileSession
from launcher.process import ProcessLauncher
from launcher.profile import ProfileSessionController
from repository_launcher_test_support import FakeSpawnedProcess
from repository_launcher_linux_process_tests import LauncherLinuxProcessStopTests
from repository_launcher_build_boundary_tests import LauncherBuildBoundaryTests


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
LIT_WAIT = "wait"
LIT_TERMINATE = "terminate"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_BUILTINS_PRINT = "builtins.print"
LIT_MAIN = "__main__"


class LauncherProfileProcessTests(unittest.TestCase):
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


if __name__ == LIT_MAIN:
    unittest.main()
