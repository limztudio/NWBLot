#!/usr/bin/env python3
"""Exercise custom controls through Win32/X11 input and validate displayed model markers.

Windows uses native window messages; Shift+Tab also uses a real Shift modifier
while the fixture is foreground. X11 uses XSendEvent and actual focus transfers.
This harness does not qualify physical pointer grabs or Wayland input.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import math
from pathlib import Path
import platform
import re
import sys
import time

SMOKE_DIRECTORY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SMOKE_DIRECTORY))

from window_capture_smoke import (  # noqa: E402
    LinuxXEvent, SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip,
    build_launch_environment, collect_log_delta, create_capture_backend, ensure_process_running,
    launch_logserver, launch_testbed, read_bmp_24_rows, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_expected_log_text,
    wait_for_log_message,
)

DISPLAY_PATTERN = re.compile(
    r"UiInteractiveSmoke: display logical=([0-9.eE+-]+)x([0-9.eE+-]+) scale=([0-9.eE+-]+)x([0-9.eE+-]+)"
)
ACTION_PATTERN = re.compile(
    r"UiInteractiveSmoke: action=(increase|enabled|reset) count=(\d+) enabled=([01]) actions=(\d+)"
)
EXPECTED_ACTIONS = [
    ("increase", 1, 1, 1), ("increase", 2, 1, 2), ("increase", 3, 1, 3),
    ("enabled", 3, 0, 4), ("enabled", 3, 1, 5), ("increase", 4, 1, 6),
    ("increase", 5, 1, 7), ("increase", 6, 1, 8), ("reset", 0, 1, 9),
]
VIRTUAL_KEYS = {"Tab": 0x09, "Return": 0x0D, "space": 0x20, "Escape": 0x1B}
OFF = (0.08, 0.01, 0.01)
ON = (0.02, 0.70, 0.12)
MARKER_TOLERANCE = 4


class WinKeyboardInput(ctypes.Structure):
    _fields_ = [("virtual_key", ctypes.c_uint16), ("scan", ctypes.c_uint16),
        ("flags", ctypes.c_uint32), ("time", ctypes.c_uint32), ("extra", ctypes.c_void_p)]


class WinMouseInput(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int32), ("y", ctypes.c_int32), ("data", ctypes.c_uint32),
        ("flags", ctypes.c_uint32), ("time", ctypes.c_uint32), ("extra", ctypes.c_void_p)]


class WinInputUnion(ctypes.Union):
    _fields_ = [("keyboard", WinKeyboardInput), ("mouse", WinMouseInput)]


class WinInput(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint32), ("data", WinInputUnion)]


def parse_args(argv, *, description=__doc__):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--timeout", type=float, default=90.0)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0.0:
        parser.error("--timeout must be finite and positive")
    args.executable = args.executable.resolve()
    args.working_directory = args.working_directory.resolve()
    args.output_directory = args.output_directory.resolve()
    args.window_title = "NWB UI Layer Smoke"
    args.no_logserver = False
    args.log_port = 0
    args.software_vulkan = "off"
    args.application_arg = ["--gpudbg", *args.application_arg]
    return args


def linear_rgb_bytes(color):
    def convert(channel):
        srgb = channel * 12.92 if channel <= 0.0031308 else 1.055 * channel ** (1.0 / 2.4) - 0.055
        return int(round(srgb * 255.0))
    return tuple(convert(channel) for channel in color)


def observe_markers(frame, scale_x, scale_y, expected_count, enabled):
    width, height, rows = frame
    probes = []
    for bit in range(9):
        logical_x = 25.0 + bit * 20.0 if bit < 8 else 224.0
        x = int(round(logical_x * scale_x))
        y = int(round(height - 14.0 * scale_y))
        if not 1 <= x < width - 1 or not 1 <= y < height - 1:
            raise SmokeFailure(f"model marker falls outside {width}x{height} at ({x},{y})")
        wanted = bool(expected_count & (1 << bit)) if bit < 8 else enabled
        expected = linear_rgb_bytes(ON if wanted else OFF)
        sample = [rows[py][px] for py in range(y - 1, y + 2) for px in range(x - 1, x + 2)]
        observed = tuple(sorted(pixel[channel] for pixel in sample)[4] for channel in range(3))
        error = max(abs(actual - reference) for actual, reference in zip(observed, expected))
        probes.append({"bit": bit if bit < 8 else "enabled", "position": [x, y],
            "expected": list(expected), "observed": list(observed), "error": error,
            "passed": error <= MARKER_TOLERANCE})
    return {"extent": [width, height], "count": expected_count, "enabled": enabled,
        "probes": probes, "passed": all(probe["passed"] for probe in probes)}


class NativeInput:
    def __init__(self, backend, handle):
        self.backend = backend
        self.handle = handle
        self.windows = platform.system() == "Windows"
        if self.windows:
            backend.user32.GetForegroundWindow.argtypes = []
            backend.user32.GetForegroundWindow.restype = ctypes.c_void_p
            backend.user32.SendInput.argtypes = [ctypes.c_uint32, ctypes.POINTER(WinInput), ctypes.c_int]
            backend.user32.SendInput.restype = ctypes.c_uint32

    def _post(self, message, parameter=0, data=0):
        if not self.backend.user32.PostMessageW(ctypes.c_void_p(self.handle), message, parameter, data):
            raise SmokeFailure(f"PostMessageW failed for message {message:#x}")

    def pointer(self, x, y):
        if self.windows:
            self._post(0x0200, 0, ((int(y) & 0xFFFF) << 16) | (int(x) & 0xFFFF))
        else:
            self.backend.send_motion_event(self.handle, int(x), int(y))

    def button(self, down, x, y):
        self.pointer(x, y)
        if self.windows:
            self._post(0x0201 if down else 0x0202, 1 if down else 0,
                ((int(y) & 0xFFFF) << 16) | (int(x) & 0xFFFF))
        else:
            event_type = self.backend.BUTTON_PRESS if down else self.backend.BUTTON_RELEASE
            self.backend.send_button_event(self.handle, event_type, int(x), int(y))

    def click(self, x, y):
        self.button(True, x, y)
        time.sleep(0.08)
        self.button(False, x, y)

    def key(self, name, down, *, repeat=False, shift=False):
        if self.windows:
            self._post(0x0100 if down else 0x0101, VIRTUAL_KEYS[name],
                1 | ((1 << 30) if repeat or not down else 0) | ((1 << 31) if not down else 0))
            return
        keysym = self.backend.x11.XStringToKeysym(name.encode("ascii"))
        keycode = self.backend.x11.XKeysymToKeycode(self.backend.display, keysym)
        if not keycode:
            raise SmokeFailure(f"could not resolve X11 key '{name}'")
        event = LinuxXEvent()
        event.xkey.type = self.backend.KEY_PRESS if down else self.backend.KEY_RELEASE
        self.backend._fill_input_event_prefix(event.xkey, self.handle, 0, 0, state=1 if shift else 0)
        event.xkey.keycode = keycode
        mask = self.backend.KEY_PRESS_MASK if down else self.backend.KEY_RELEASE_MASK
        if not self.backend.x11.XSendEvent(self.backend.display, self.handle, False, mask, ctypes.byref(event)):
            raise SmokeFailure(f"failed to send X11 key '{name}'")
        self.backend.x11.XFlush(self.backend.display)

    def _shift(self, down):
        if not self.windows:
            return
        event = WinInput(type=1)
        event.data.keyboard = WinKeyboardInput(0x10, 0, 0 if down else 0x0002, 0, None)
        if self.backend.user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput)) != 1:
            raise SmokeFailure("SendInput failed for Shift modifier")

    def tap(self, name, *, repeat_count=0, shift=False):
        if shift and self.windows:
            self.backend.focus_window(self.handle)
            time.sleep(0.1)
            if self.backend.user32.GetForegroundWindow() != self.handle:
                raise SmokeSkip("the fixture cannot take foreground focus for native Shift+Tab")
            self._shift(True)
            time.sleep(0.1)
        try:
            self.key(name, True, shift=shift)
            for _ in range(repeat_count):
                time.sleep(0.04)
                self.key(name, True, repeat=True, shift=shift)
            time.sleep(0.08)
            self.key(name, False, shift=shift)
        finally:
            if shift and self.windows:
                self._shift(False)
        time.sleep(0.1)

    def focus_loss_and_gain(self):
        if self.windows:
            self._post(0x0006, 0)  # WM_ACTIVATE / WA_INACTIVE reaches the real native lifecycle callback.
            time.sleep(0.15)
            self._post(0x0006, 1)
        else:
            self.backend.x11.XSetInputFocus(self.backend.display, self.backend.root,
                self.backend.REVERT_TO_PARENT, self.backend.CURRENT_TIME)
            self.backend.x11.XFlush(self.backend.display)
            time.sleep(0.15)
            self.backend.focus_window(self.handle)
        time.sleep(0.15)


class InteractionRun:
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        self.args = args
        self.backend = backend
        self.handle = handle
        self.process = process
        self.log_directory = log_directory
        self.log_baseline = log_baseline
        self.log_pattern = log_pattern
        self.native = NativeInput(backend, handle)
        self.deadline = time.monotonic() + args.timeout
        self.scale_x = 1.0
        self.scale_y = 1.0
        self.stages = []

    def logs(self):
        return collect_log_delta(self.log_directory, self.log_baseline, self.log_pattern)

    def update_display(self):
        matches = list(DISPLAY_PATTERN.finditer(self.logs()))
        if not matches:
            raise SmokeFailure("the interactive fixture did not publish display metrics")
        logical_width, logical_height, self.scale_x, self.scale_y = map(float, matches[-1].groups())
        if not all(math.isfinite(value) and value > 0.0
            for value in (logical_width, logical_height, self.scale_x, self.scale_y)):
            raise SmokeFailure("interactive fixture display metrics are invalid")

    def point(self, x, y):
        return round(x * self.scale_x), round(y * self.scale_y)

    def checkpoint(self, name, count, enabled, *, extent=None):
        # Unchanged-state gates also wait beyond the native event batch, catching delayed activation/replay.
        time.sleep(0.25)
        path = self.args.output_directory / f"{len(self.stages):02d}_{name}.bmp"
        stage_deadline = min(self.deadline, time.monotonic() + 10.0)
        report = None
        while time.monotonic() < stage_deadline:
            ensure_process_running(self.process, "during interactive UI capture")
            self.update_display()
            self.backend.capture_client_window(self.handle, path)
            frame = read_bmp_24_rows(path)
            report = observe_markers(frame, self.scale_x, self.scale_y, count, enabled)
            if extent is not None and tuple(frame[:2]) != extent:
                report["passed"] = False
            if report["passed"]:
                report.update({"stage": name, "capture": str(path)})
                self.stages.append(report)
                self.write_report(False)
                return
            time.sleep(0.1)
        raise SmokeFailure(f"interactive marker gate '{name}' failed: {report}")

    def write_report(self, passed):
        report = {"passed": passed, "platform": platform.system(), "stages": self.stages,
            "input_path": "Win32 posted messages with native Shift modifier; posted activation loss/gain"
                if self.native.windows else "X11 XSendEvent and XSetInputFocus",
            "runtime_limit": "No physical pointer grab or Wayland input qualification"}
        (self.args.output_directory / "interaction.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    def execute(self):
        self.checkpoint("initial", 0, True)
        time.sleep(0.2)  # Let the host publish the matching successfully presented initial hit layout.
        self.native.click(*self.point(91, 76))
        self.checkpoint("first_click", 1, True)
        self.native.tap("space", repeat_count=3)
        self.checkpoint("space_single_activation", 2, True)
        self.native.tap("Return", repeat_count=3)
        self.checkpoint("enter_single_activation", 3, True)
        self.native.tap("Tab")
        self.native.tap("space")
        self.checkpoint("keyboard_checkbox_disabled", 3, False)
        self.native.click(*self.point(91, 76))
        self.native.tap("space")
        self.checkpoint("disabled_button_barrier", 3, False)
        self.native.tap("Tab")
        self.native.tap("Tab")
        self.native.tap("Tab", shift=True)
        self.native.tap("space")
        self.checkpoint("tab_shift_tab_checkbox_enabled", 3, True)
        self.native.click(*self.point(91, 76))
        self.checkpoint("enabled_click", 4, True)
        self.native.button(True, *self.point(91, 76))
        time.sleep(0.1)
        self.native.button(False, *self.point(400, 260))
        self.checkpoint("drag_out_cancels", 4, True)
        self.native.tap("space")
        self.checkpoint("release_outside_cleanup", 5, True)
        self.native.button(True, *self.point(91, 76))
        time.sleep(0.1)
        self.native.focus_loss_and_gain()
        self.native.button(False, *self.point(91, 76))
        self.checkpoint("focus_loss_cancels_hold", 5, True)
        self.backend.resize_client(self.handle, 800, 600)
        self.checkpoint("resized", 5, True, extent=(800, 600))
        time.sleep(0.2)
        self.native.click(*self.point(91, 76))
        self.checkpoint("resized_first_click", 6, True, extent=(800, 600))
        self.native.click(*self.point(91, 124))
        self.checkpoint("reset", 0, True, extent=(800, 600))


def run(args):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_launch_environment(args)
    environment["NWB_UI_LAYER_INTERACTIVE"] = "1"
    environment["NWB_UI_LAYER_WINDOW"] = "0"
    environment["NWB_UI_LAYER_WINDOW_SKIN"] = "0"
    environment["NWB_UI_LAYER_EDIT"] = "0"
    environment["NWB_UI_LAYER_POPUP"] = "0"
    environment["NWB_UI_LAYER_POPUP_SKIN"] = "0"
    if platform.system() == "Linux":
        environment["NWB_LINUX_BACKEND"] = "x11"
    for variable in ("NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS", "NWB_GPU_TIMING_FILE"):
        environment.pop(variable, None)
    backend = create_capture_backend()
    logserver = application = interaction = None
    handle = None
    log_directory = log_baseline = log_pattern = None
    try:
        logserver, port, log_directory, log_baseline, log_pattern = launch_logserver(args, args.executable, environment)
        application = launch_testbed(args, args.executable, environment, port)
        handle = backend.wait_for_window(application.pid, args.timeout, args.window_title)
        if not handle:
            ensure_process_running(application, "before the interactive window appeared")
            raise SmokeFailure("interactive UI window did not appear")
        backend.prepare_window(handle)
        wait_for_log_message(log_directory, log_baseline, log_pattern, "UiInteractiveSmoke: display", min(args.timeout, 15.0))
        interaction = InteractionRun(args, backend, handle, application, log_directory, log_baseline, log_pattern)
        interaction.execute()
    finally:
        try:
            if application is not None:
                exit_code, tail = terminate_process(application, "UI interaction fixture", handle)
                require_normal_process_exit(exit_code, tail, "UI interaction fixture")
        finally:
            try:
                text = ""
                if log_directory is not None:
                    text = shutdown_logserver_and_collect(logserver, log_directory, log_baseline, log_pattern)
                    logserver = None
                    (args.output_directory / "interaction.log").write_text(text, encoding="utf-8")
            finally:
                if logserver is not None:
                    terminate_process(logserver, "UI interaction logserver")
                backend.close()
    validate_expected_log_text(text, ["Loader: project startup complete", "UiLayerSmokeProject: shutdown"], STRICT_LOG_FAILURE_MESSAGES)
    observed = [(action, int(count), int(enabled), int(sequence))
        for action, count, enabled, sequence in ACTION_PATTERN.findall(text)]
    if observed != EXPECTED_ACTIONS:
        raise SmokeFailure(f"unexpected interactive action sequence: {observed}; expected {EXPECTED_ACTIONS}")
    interaction.write_report(True)
    print(f"UI interaction: {len(interaction.stages)} displayed gates, {len(EXPECTED_ACTIONS)} exact actions; no replay", flush=True)
    return 0


def main(argv):
    args = parse_args(argv)
    try:
        return run(args)
    except SmokeSkip as error:
        print(f"SKIP: {error}", flush=True)
        return SKIP_EXIT_CODE
    except (SmokeFailure, OSError, ValueError, TimeoutError) as error:
        print(f"FAIL: {error}", flush=True)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
