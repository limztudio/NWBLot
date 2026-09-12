#!/usr/bin/env python3
import ctypes
from dataclasses import dataclass
import math
import ntpath
import os
from typing import Callable, Optional, Tuple


WINDOWS_ENUM_CALLBACK = getattr(ctypes, "WINFUNCTYPE", ctypes.CFUNCTYPE)(
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_ssize_t,
)
PROCESS_TERMINATE = 0x0001
SYNCHRONIZE = 0x00100000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_QUERY_AND_WAIT_ACCESS = SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
PROCESS_QUERY_WAIT_AND_TERMINATE_ACCESS = PROCESS_QUERY_AND_WAIT_ACCESS | PROCESS_TERMINATE
WM_CLOSE = 0x0010
WAIT_OBJECT_0 = 0x00000000
WAIT_TIMEOUT = 0x00000102
INFINITE_TIMEOUT_LIMIT = 0xFFFFFFFE
FORCED_EXIT_CODE = 1
MAX_WINDOWS_IMAGE_PATH = 32768


@dataclass(frozen=True)
class WindowsProcessStopResult:
    pid: int
    exit_code: int
    graceful_close_requested: bool
    forced: bool


@dataclass(frozen=True)
class WindowsProcessHandle:
    native_handle: int
    can_terminate: bool


@dataclass(frozen=True)
class WindowsBoundedRunResult:
    pid: int
    exit_code: int
    deadline_reached: bool
    graceful_close_requested: bool
    forced: bool


class WindowsProcessError(RuntimeError):
    pass


class WindowsProcessApi:
    def __init__(self):
        self._kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self._psapi = ctypes.WinDLL("psapi", use_last_error=True)
        self._user32 = ctypes.WinDLL("user32", use_last_error=True)
        self._bind_functions()

    def process_ids(self) -> Tuple[int, ...]:
        capacity = 1024
        while True:
            process_ids = (ctypes.c_uint32 * capacity)()
            bytes_written = ctypes.c_uint32(0)
            if not self._psapi.EnumProcesses(process_ids, ctypes.sizeof(process_ids), ctypes.byref(bytes_written)):
                raise WindowsProcessError("EnumProcesses failed")

            count = bytes_written.value // ctypes.sizeof(ctypes.c_uint32)
            if count < capacity:
                return tuple(int(process_ids[index]) for index in range(count))
            capacity *= 2

    def open_process(self, pid: int) -> Optional[WindowsProcessHandle]:
        handle = self._kernel32.OpenProcess(PROCESS_QUERY_WAIT_AND_TERMINATE_ACCESS, False, pid)
        if handle:
            return WindowsProcessHandle(int(handle), True)

        handle = self._kernel32.OpenProcess(PROCESS_QUERY_AND_WAIT_ACCESS, False, pid)
        return WindowsProcessHandle(int(handle), False) if handle else None

    def query_process_image_path(self, handle: WindowsProcessHandle) -> Optional[str]:
        buffer = ctypes.create_unicode_buffer(MAX_WINDOWS_IMAGE_PATH)
        length = ctypes.c_uint32(len(buffer))
        if not self._kernel32.QueryFullProcessImageNameW(handle.native_handle, 0, buffer, ctypes.byref(length)):
            return None
        return buffer.value[:length.value]

    def request_close(self, pid: int) -> bool:
        posted = [False]

        @WINDOWS_ENUM_CALLBACK
        def enum_window(hwnd, _lparam):
            owner_pid = ctypes.c_uint32(0)
            self._user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner_pid))
            if owner_pid.value == pid and self._user32.PostMessageW(hwnd, WM_CLOSE, 0, 0):
                posted[0] = True
            return True

        if not self._user32.EnumWindows(enum_window, 0):
            return False
        return posted[0]

    def wait_for_exit(self, handle: WindowsProcessHandle, timeout_seconds: float) -> bool:
        timeout_milliseconds = min(INFINITE_TIMEOUT_LIMIT, max(0, math.ceil(timeout_seconds * 1000.0)))
        result = self._kernel32.WaitForSingleObject(handle.native_handle, timeout_milliseconds)
        if result == WAIT_OBJECT_0:
            return True
        if result == WAIT_TIMEOUT:
            return False
        raise WindowsProcessError("WaitForSingleObject failed")

    def force_terminate(self, handle: WindowsProcessHandle) -> bool:
        if not handle.can_terminate:
            return False
        return bool(self._kernel32.TerminateProcess(handle.native_handle, FORCED_EXIT_CODE))

    def exit_code(self, handle: WindowsProcessHandle) -> int:
        exit_code = ctypes.c_uint32(0)
        if not self._kernel32.GetExitCodeProcess(handle.native_handle, ctypes.byref(exit_code)):
            raise WindowsProcessError("GetExitCodeProcess failed")
        return int(exit_code.value)

    def close_process(self, handle: WindowsProcessHandle) -> None:
        # Handle release is terminal cleanup. It must never replace a more useful query/wait/termination failure.
        self._kernel32.CloseHandle(handle.native_handle)

    def _bind_functions(self) -> None:
        self._psapi.EnumProcesses.argtypes = [
            ctypes.POINTER(ctypes.c_uint32),
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self._psapi.EnumProcesses.restype = ctypes.c_int
        self._kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        self._kernel32.OpenProcess.restype = ctypes.c_void_p
        self._kernel32.QueryFullProcessImageNameW.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint32,
            ctypes.c_wchar_p,
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self._kernel32.QueryFullProcessImageNameW.restype = ctypes.c_int
        self._kernel32.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        self._kernel32.WaitForSingleObject.restype = ctypes.c_uint32
        self._kernel32.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        self._kernel32.TerminateProcess.restype = ctypes.c_int
        self._kernel32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        self._kernel32.GetExitCodeProcess.restype = ctypes.c_int
        self._kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
        self._kernel32.CloseHandle.restype = ctypes.c_int
        self._user32.EnumWindows.argtypes = [WINDOWS_ENUM_CALLBACK, ctypes.c_ssize_t]
        self._user32.EnumWindows.restype = ctypes.c_int
        self._user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
        self._user32.GetWindowThreadProcessId.restype = ctypes.c_uint32
        self._user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_size_t, ctypes.c_ssize_t]
        self._user32.PostMessageW.restype = ctypes.c_int


def normalize_windows_image_path(path, resolve_path: Optional[Callable[[str], str]] = None) -> str:
    resolver = resolve_path or os.path.realpath
    resolved = ntpath.normcase(ntpath.normpath(os.fspath(resolver(os.fspath(path)))))
    if resolved.startswith("\\\\?\\unc\\"):
        return "\\\\" + resolved[8:]
    if resolved.startswith("\\\\?\\"):
        return resolved[4:]
    return resolved


def stop_processes_by_image_path(
    executable,
    grace_timeout_seconds: float,
    hard_timeout_seconds: float,
    api=None,
    resolve_path: Optional[Callable[[str], str]] = None,
    current_process_id: Optional[int] = None,
) -> Tuple[WindowsProcessStopResult, ...]:
    process_api = api or WindowsProcessApi()
    target_image_path = normalize_windows_image_path(executable, resolve_path)
    own_pid = os.getpid() if current_process_id is None else current_process_id
    results = []

    for pid in process_api.process_ids():
        if pid == 0 or pid == own_pid:
            continue

        handle = process_api.open_process(pid)
        if handle is None:
            continue

        try:
            image_path = process_api.query_process_image_path(handle)
            if image_path is None or normalize_windows_image_path(image_path, resolve_path) != target_image_path:
                continue

            graceful_close_requested = process_api.request_close(pid)
            forced = False
            if not process_api.wait_for_exit(handle, grace_timeout_seconds):
                forced = process_api.force_terminate(handle)
                if not forced and not process_api.wait_for_exit(handle, 0.0):
                    raise WindowsProcessError(f"process {pid} did not grant terminate access and did not exit gracefully")
                if not process_api.wait_for_exit(handle, hard_timeout_seconds):
                    raise WindowsProcessError(f"process {pid} did not exit after TerminateProcess")

            results.append(
                WindowsProcessStopResult(
                    pid,
                    process_api.exit_code(handle),
                    graceful_close_requested,
                    forced,
                )
            )
        finally:
            process_api.close_process(handle)

    return tuple(results)


def run_bounded_process(
    process,
    run_seconds: float,
    grace_timeout_seconds: float,
    hard_timeout_seconds: float,
    api=None,
) -> WindowsBoundedRunResult:
    process_api = api or WindowsProcessApi()
    handle = process_api.open_process(process.pid)
    if handle is None:
        exit_code = process.poll()
        if exit_code is None:
            raise WindowsProcessError(f"could not open launched process {process.pid}")
        return WindowsBoundedRunResult(process.pid, exit_code, False, False, False)

    try:
        if process_api.wait_for_exit(handle, run_seconds):
            return WindowsBoundedRunResult(process.pid, process.wait(), False, False, False)

        graceful_close_requested = process_api.request_close(process.pid)
        if process_api.wait_for_exit(handle, grace_timeout_seconds):
            return WindowsBoundedRunResult(process.pid, process.wait(), True, graceful_close_requested, False)

        forced = process_api.force_terminate(handle)
        if not forced and not process_api.wait_for_exit(handle, 0.0):
            raise WindowsProcessError(
                f"launched process {process.pid} did not grant terminate access and did not exit gracefully"
            )
        if not process_api.wait_for_exit(handle, hard_timeout_seconds):
            raise WindowsProcessError(f"launched process {process.pid} did not exit after TerminateProcess")
        exit_code = process.wait()
        if forced and exit_code == 0:
            raise WindowsProcessError(f"forced process {process.pid} reported a successful exit status")
        return WindowsBoundedRunResult(process.pid, exit_code, True, graceful_close_requested, forced)
    finally:
        process_api.close_process(handle)
