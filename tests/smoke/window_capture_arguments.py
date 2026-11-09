"""Parse and validate window capture command-line arguments."""

import argparse
import math
from pathlib import Path

from window_capture_image import TRANSPARENT_CSG_REGIONS

LIT_MINIMUM_CAPTURE_SETTLE_SECONDS = 1.0
LIT_GPUDBG = "--gpudbg"
LIT_RESIZE_SETTLE_SECONDS = "--resize-settle-seconds"
LIT_OFF = "off"
LIT_ON = "on"
LIT_TIMEOUT = "--timeout"
LIT_SETTLE_SECONDS = "--settle-seconds"
LIT_RENDER_READY_TIMEOUT = "--render-ready-timeout"
LIT_STORE_TRUE = "store_true"
LIT_APPLICATION_CAPTURE_FRAME_COUNT = "--application-capture-frame-count"
LIT_APPEND = "append"
RENDER_READY_TIMEOUT_SECONDS = 10.0


class WindowCaptureArguments:
    def __init__(self, strict_log_failure_messages):
        self.strict_log_failure_messages = strict_log_failure_messages

    @staticmethod
    def parse_int(value):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(f"invalid integer value '{value}'") from exc

    @staticmethod
    def require_positive_arg(parser, name, value):
        if value <= 0.0:
            parser.error(f"{name} must be positive")

    @staticmethod
    def require_non_negative_arg(parser, name, value):
        if value < 0.0:
            parser.error(f"{name} must not be negative")

    def parse_args(self, argv):
        parser = argparse.ArgumentParser(description="Launch an NWB executable and capture its rendered output.")
        parser.add_argument("--executable", help="Path to the executable. Optional when --window-handle is used.")
        parser.add_argument("--working-directory", type=Path, default=Path.cwd(), help="Working directory for launched processes.")
        parser.add_argument("--output", type=Path, required=True, help="Screenshot output path. The script writes a 24-bit BMP.")
        parser.add_argument("--window-handle", type=WindowCaptureArguments.parse_int, help="Capture an existing native window handle instead of launching testbed.")
        parser.add_argument("--window-title", default="", help="Expected window title when matching a launched testbed window.")
        parser.add_argument(LIT_TIMEOUT, type=float, default=45.0, help="Seconds to wait for logserver and the testbed window.")
        parser.add_argument("--graceful-exit-timeout", type=float, default=10.0, help="Seconds to wait for application shutdown after a completed capture.")
        parser.add_argument(LIT_SETTLE_SECONDS, type=float, default=2.0, help="Seconds to wait after the window becomes visible.")
        parser.add_argument("--resize-client", nargs=2, type=int, metavar=("WIDTH", "HEIGHT"), help="After rendering and settling at the original extent, resize the launched client once and verify its framebuffer extent.")
        parser.add_argument(LIT_RESIZE_SETTLE_SECONDS, type=float, help="Seconds to render after the requested client/framebuffer resize is confirmed; defaults to 2, minimum 1.")
        parser.add_argument("--expect-before-resize-log-message", help="Wait for this runtime marker before capturing the original size.")
        parser.add_argument(LIT_RENDER_READY_TIMEOUT, type=float, default=RENDER_READY_TIMEOUT_SECONDS, help="Seconds to wait for a blank visible window to publish its first rendered frame.",)
        parser.add_argument("--application-capture", action=LIT_STORE_TRUE, help="Use the application's framebuffer readback instead of desktop window capture.",)
        parser.add_argument(LIT_APPLICATION_CAPTURE_FRAME_COUNT, type=int, default=360, help="Positive rendered frame count at which application capture is requested. Defaults to 360.",)
        parser.add_argument("--logserver-executable", help="Path to nwb_logserver/logserver. Defaults to a sibling of --executable.")
        parser.add_argument("--no-logserver", action=LIT_STORE_TRUE, help="Do not start a logserver; launch with standalone log output.")
        parser.add_argument("--gpu-validation", action=argparse.BooleanOptionalAction, default=False, help="Request graphics validation from a launched dbg/opt application; fin applications reject the request.")
        parser.add_argument("--log-port", type=int, default=0, help="Logserver port. Defaults to an available localhost port.")
        parser.add_argument("--log-output", type=Path, help="For application capture, save the exact collected per-launch log text before validation.")
        parser.add_argument("--expect-log-message", action=LIT_APPEND, default=[], help="Required substring in the logserver output.")
        parser.add_argument("--reject-log-message", action=LIT_APPEND, default=list(self.strict_log_failure_messages), help="Forbidden substring in the logserver output. Runtime warnings, assertions, errors, and validation failures are always rejected.",)
        parser.add_argument("--skip-log-message", action=LIT_APPEND, default=[], help="Log substring that classifies the launched smoke as unsupported instead of failed.",)
        parser.add_argument("--skip-blocking-log-message", action=LIT_APPEND, default=list(self.strict_log_failure_messages),
            help="Log substring that remains a failure even when an explicit capability-skip marker is present. Runtime warnings, assertions, errors, and validation failures always block skips.",)
        parser.add_argument("--expect-transparent-multi", action=LIT_STORE_TRUE, help="Assert that the captured scene contains multiple distinct transparent mesh colors.",)
        parser.add_argument("--expect-transparent-csg", action=LIT_STORE_TRUE, help="Assert that the captured transparent center mesh contains a clipped CSG void.",)
        parser.add_argument("--transparent-csg-pose", choices=tuple(TRANSPARENT_CSG_REGIONS), help="Required with --expect-transparent-csg: early/mid/late selects the fixture's frozen yaw 0/1/2 interior regions.",)
        parser.add_argument("--expect-texture-smoke", action=LIT_STORE_TRUE, help="Assert that the captured scene visibly samples the authored red, green, and blue texture pattern.",)
        parser.add_argument("--expect-surfel-coverage-probe", action=LIT_STORE_TRUE, help="Assert that the closed GI scene's perpendicular same-cell probe faces receive surfel coverage.",)
        parser.add_argument("--software-vulkan", choices=("auto", LIT_ON, LIT_OFF), default=LIT_OFF, help="On Linux, optionally use Mesa lavapipe. Defaults to the system Vulkan ICD.",)
        parser.add_argument("--application-arg", action=LIT_APPEND, default=[], help="Extra argument to pass to the launched application. Repeat for multiple arguments.",)
        args = parser.parse_args(argv)

        if args.expect_transparent_csg and args.transparent_csg_pose is None:
            parser.error("--expect-transparent-csg requires --transparent-csg-pose")
        if args.transparent_csg_pose is not None and not args.expect_transparent_csg:
            parser.error("--transparent-csg-pose requires --expect-transparent-csg")
        if args.gpu_validation and args.window_handle is not None:
            parser.error("--gpu-validation requires launching an executable, not --window-handle")
        if args.application_capture and args.window_handle is not None:
            parser.error("--application-capture cannot be combined with --window-handle")
        if args.log_output and not args.application_capture:
            parser.error("--log-output requires --application-capture")
        if args.window_handle is None and not args.executable:
            parser.error("--executable is required unless --window-handle is provided")
        for name, value in ((LIT_TIMEOUT, args.timeout), ("--graceful-exit-timeout", args.graceful_exit_timeout), (LIT_RENDER_READY_TIMEOUT, args.render_ready_timeout), (LIT_SETTLE_SECONDS, args.settle_seconds), (LIT_RESIZE_SETTLE_SECONDS, args.resize_settle_seconds)):
            if value is not None and not math.isfinite(value):
                parser.error(f"{name} must be finite")
        WindowCaptureArguments.require_positive_arg(parser, "--graceful-exit-timeout", args.graceful_exit_timeout)
        WindowCaptureArguments.require_positive_arg(parser, LIT_TIMEOUT, args.timeout)
        WindowCaptureArguments.require_positive_arg(parser, LIT_RENDER_READY_TIMEOUT, args.render_ready_timeout)
        WindowCaptureArguments.require_positive_arg(parser, LIT_APPLICATION_CAPTURE_FRAME_COUNT, args.application_capture_frame_count,)
        WindowCaptureArguments.require_non_negative_arg(parser, LIT_SETTLE_SECONDS, args.settle_seconds)

        if args.expect_before_resize_log_message and args.resize_client is None:
            parser.error("--expect-before-resize-log-message requires --resize-client")
        if args.resize_client is not None:
            if args.application_capture or args.window_handle is not None:
                parser.error("--resize-client requires a launched desktop capture; application capture and existing handles are incompatible")
            if any(dimension < 1 or dimension > 16384 for dimension in args.resize_client):
                parser.error("--resize-client dimensions must be between 1 and 16384")
            if args.settle_seconds < LIT_MINIMUM_CAPTURE_SETTLE_SECONDS:
                parser.error("--resize-client requires --settle-seconds to be at least 1")
            if args.resize_settle_seconds is None:
                args.resize_settle_seconds = 2.0
            if args.resize_settle_seconds < LIT_MINIMUM_CAPTURE_SETTLE_SECONDS:
                parser.error("--resize-settle-seconds must be at least 1")
        elif args.resize_settle_seconds is not None:
            parser.error("--resize-settle-seconds requires --resize-client")

        args.working_directory = args.working_directory.resolve()
        args.output = args.output.resolve()
        if args.gpu_validation:
            args.application_arg = [LIT_GPUDBG, *args.application_arg]
        return args
