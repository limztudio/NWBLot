"""Linux process stop and pidfd ownership tests."""

import sys
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from launcher import constants as launcher
from launcher.process import ProcessLauncher

LIT_APPLICATION = "application"


class LauncherLinuxProcessStopTests(unittest.TestCase):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select), mock.patch("builtins.open", mock.mock_open(read_data="Kthread:\t0\n")):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select), mock.patch("builtins.open", mock.mock_open(read_data="Kthread:\t1\n")):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select), mock.patch("builtins.open", side_effect=[status, stat]):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal), mock.patch("launcher.linux_process_stopper.select", fake_select):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal):
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
        with mock.patch("launcher.linux_process_stopper.os", fake_os), mock.patch("launcher.linux_process_stopper.signal", fake_signal):
            with self.assertRaisesRegex(SystemExit, "ambiguously named"):
                ProcessLauncher.stop_existing_process(target, launcher.PLATFORM_LINUX)
        fake_signal.pidfd_send_signal.assert_not_called()
        fake_os.close.assert_called_once_with(11)


    def test_linux_missing_pidfd_support_rejects_stop(self):
        fake_os = mock.Mock(spec=["listdir"])
        with mock.patch("launcher.linux_process_stopper.os", fake_os):
            with self.assertRaisesRegex(SystemExit, "requires Linux pidfd support"):
                ProcessLauncher.stop_existing_process(Path(LIT_APPLICATION), launcher.PLATFORM_LINUX)



if __name__ == "__main__":
    unittest.main()
