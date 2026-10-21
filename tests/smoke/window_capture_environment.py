"""Window-capture launch environment: runtime argument mapping, sibling executables, Lavapipe selection, and application-capture environment preparation."""


import argparse
import math
import os
from pathlib import Path
from types import SimpleNamespace
import platform
from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure, write_status  # noqa: E402
from window_capture_win32 import bind_windows_user32, create_windows_user32, WinRect, WinPoint, WindowsUserObjectFlags, require_windows_interactive_desktop, WindowsCapture  # noqa: E402


LIT_GPUDBG = "--gpudbg"
LIT_WINDOWS = "Windows"
LIT_LINUX = "Linux"
LIT_OFF = "off"
LIT_ON = "on"
LIT_RESIZE_CLIENT = "resize_client"
FRAMEBUFFER_CAPTURE_PATH_ENV = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH"
FRAMEBUFFER_CAPTURE_FRAME_COUNT_ENV = "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT"


def executable_sibling(executable, name):
    suffix = ".exe" if platform.system() == LIT_WINDOWS else ""
    candidate = executable.parent / f"{name}{suffix}"
    return candidate if candidate.exists() else None


def resolve_lavapipe_icd():
    candidates = (Path("/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"), Path("/usr/share/vulkan/icd.d/lvp_icd.i686.json"), Path("/etc/vulkan/icd.d/lvp_icd.x86_64.json"), Path("/etc/vulkan/icd.d/lvp_icd.i686.json"),)
    for candidate in candidates:
        if candidate.exists():
            return candidate

    return None


def make_runtime_launch_args(args: argparse.Namespace) -> SimpleNamespace:
    return SimpleNamespace(no_logserver=args.no_logserver, logserver_executable=args.logserver_executable, log_port=0, working_directory=args.runtime_dir, timeout=args.startup_timeout, application_arg=[LIT_GPUDBG] if args.gpu_validation else [],
        software_vulkan=LIT_OFF,)


def build_launch_environment(args):
    env = os.environ.copy()
    system = platform.system()

    if system == LIT_WINDOWS:
        require_windows_interactive_desktop()
    elif system == LIT_LINUX:
        if not env.get("DISPLAY"):
            raise SmokeSkip("DISPLAY is not set; X11 window capture is unavailable")

        env.setdefault("NWB_LINUX_BACKEND", "x11")
        if args.software_vulkan != LIT_OFF:
            lavapipe_icd = resolve_lavapipe_icd()
            if lavapipe_icd:
                env["VK_ICD_FILENAMES"] = str(lavapipe_icd)
            elif args.software_vulkan == LIT_ON:
                raise SmokeSkip("Mesa lavapipe Vulkan ICD was requested but is not installed")

    if getattr(args, LIT_RESIZE_CLIENT, None):
        validate_resize_environment(env)
    return env


def validate_resize_environment(environment):
    if environment.get(FRAMEBUFFER_CAPTURE_PATH_ENV):
        raise SmokeFailure("--resize-client cannot run with an application framebuffer capture request")
    for name in ("NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME"):
        value = environment.get(name)
        if value is None:
            continue
        try:
            frame = float(value)
        except ValueError as error:
            raise SmokeFailure(f"--resize-client requires {name} to be unset or a disabled numeric value") from error
        if not math.isfinite(frame) or frame >= 1.0:
            raise SmokeFailure(f"--resize-client cannot run with active or invalid {name}={value!r}")


def application_capture_partial_path(output_path):
    return Path(f"{output_path}.partial")


def prepare_application_capture_environment(args, base_environment):
    try:
        args.output.parent.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise SmokeFailure(f"failed to create application capture directory '{args.output.parent}': {error}") from error

    partial_path = application_capture_partial_path(args.output)
    for artifact_path in (args.output, partial_path):
        try:
            artifact_path.unlink()
        except FileNotFoundError:
            pass
        except OSError as error:
            raise SmokeFailure(f"failed to remove stale application capture artifact '{artifact_path}': {error}") from error

    env = base_environment.copy()
    env[FRAMEBUFFER_CAPTURE_PATH_ENV] = str(args.output)
    env[FRAMEBUFFER_CAPTURE_FRAME_COUNT_ENV] = str(args.application_capture_frame_count)
    return env
