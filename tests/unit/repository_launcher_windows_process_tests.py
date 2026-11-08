import argparse
import ctypes
import os
import subprocess
import unittest
from pathlib import Path
from unittest import mock

from launcher import constants as launcher
from launcher import repository_windows_process
from launcher.host import HostProbe
from launcher.process import ProcessLauncher
from repository_launcher_test_support import FakeSpawnedProcess


LIT_WINDOWSPROCESSAPI = "WindowsProcessApi"
LIT_REALPATH = "realpath"
LIT_GETPID = "getpid"
LIT_TERMINATE_PROCESS = "terminate_process"
LIT_OPEN = "open"
LIT_CLOSE = "close"
LIT_WAIT = "wait"
LIT_FORCE = "force"
LIT_RELEASE = "release"
LIT_TERMINATE = "terminate"
LIT_KILL = "kill"
LIT_NATIVE_WAIT = "native_wait"
LIT_WINDOWS = "windows"
LIT_C_BUILD_ARM64_VIEWER_EXE = r"C:\build\arm64\viewer.exe"
LIT_POPEN = "Popen"
LIT_HOST_PLATFORM_NAME = "host_platform_name"
LIT_WINDOWS_DLL = "WinDLL"
LIT_WINDOWS_LAST_ERROR = "get_last_error"


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


class LauncherWindowsProcessTests(unittest.TestCase):
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
    def test_windows_image_query_failure_only_ignores_an_exited_process(self):
        kernel32 = mock.Mock()
        kernel32.QueryFullProcessImageNameW.return_value = 0
        with mock.patch.object(ctypes, LIT_WINDOWS_DLL, side_effect=[kernel32, mock.Mock(), mock.Mock()], create=True):
            api = repository_windows_process.WindowsProcessApi()
        handle = repository_windows_process.WindowsProcessHandle(707, False)
        with mock.patch.object(ctypes, LIT_WINDOWS_LAST_ERROR, return_value=5, create=True):
            kernel32.WaitForSingleObject.return_value = repository_windows_process.WAIT_OBJECT_0
            self.assertIsNone(api.query_process_image_path(handle))
            kernel32.WaitForSingleObject.return_value = repository_windows_process.WAIT_TIMEOUT
            with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "image query.*Windows error 5"):
                api.query_process_image_path(handle)
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
    def test_windows_enum_failure_does_not_force_existing_process(self):
        api = FakeWindowsProcessApi({202: LIT_C_BUILD_ARM64_VIEWER_EXE}, {})
        user32 = mock.Mock()
        user32.EnumWindows.return_value = 0
        native_api = object.__new__(repository_windows_process.WindowsProcessApi)
        native_api._user32 = user32
        api.request_close = native_api.request_close
        with (mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=api), mock.patch.object(os.path, LIT_REALPATH, side_effect=lambda path: path), mock.patch.object(os, LIT_GETPID, return_value=999),
            mock.patch.object(ctypes, LIT_WINDOWS_LAST_ERROR, return_value=5, create=True)):
            with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "EnumWindows failed: Windows error 5"):
                repository_windows_process.stop_processes_by_image_path(LIT_C_BUILD_ARM64_VIEWER_EXE, 3.0, 2.0)
        self.assertIn((LIT_RELEASE, api.handles[202]), api.events)
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
        process = FakeSpawnedProcess(302, 23, events)

        with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, True], events, can_terminate=False)):
            result = repository_windows_process.run_bounded_process(process, 8.0, 4.0, 2.0)

        self.assertEqual(23, result.exit_code)
        self.assertFalse(result.forced)
        self.assertFalse(any(event[0] == LIT_FORCE for event in events))
        self.assertNotIn((LIT_TERMINATE, 302), events)
        self.assertNotIn((LIT_KILL, 302), events)
    def test_windows_query_only_handle_reports_live_process_at_hard_boundary(self):
        events = []
        process = FakeSpawnedProcess(306, 0, events)

        with self.assertRaisesRegex(repository_windows_process.WindowsProcessError, "did not grant terminate access"):
            with mock.patch.object(repository_windows_process, LIT_WINDOWSPROCESSAPI, return_value=FakeBoundedProcessApi([False, False, False], events, can_terminate=False)):
                repository_windows_process.run_bounded_process(process, 8.0, 4.0, 2.0)

        retained_handle = next(event[2] for event in events if event[0] == LIT_OPEN)
        self.assertIn((LIT_FORCE, retained_handle), events)
        self.assertEqual(1, sum(event[0] == LIT_OPEN for event in events))
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

        with (mock.patch.object(subprocess, LIT_POPEN, return_value=process), mock.patch.object(HostProbe, LIT_HOST_PLATFORM_NAME, return_value=LIT_WINDOWS),
            mock.patch.object(repository_windows_process, "run_bounded_process", return_value=run_result) as bounded, mock.patch.object(ProcessLauncher, LIT_TERMINATE_PROCESS) as generic_terminate,):
            exit_code = ProcessLauncher.launch_process(args, Path(LIT_C_BUILD_ARM64_VIEWER_EXE), Path(r"C:\build\arm64"), {}, (), paths_validated=True,)

        self.assertEqual(23, exit_code)
        bounded.assert_called_once_with(process, 9.0, launcher.APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS, launcher.APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,)
        generic_terminate.assert_not_called()
