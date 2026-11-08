"""Capture, validate and resize rendered desktop windows."""

import time
from types import SimpleNamespace

from window_capture_logs import collect_log_delta, wait_for_log_message
from window_capture_outcome import SmokeFailure, write_status
from window_capture_validation import validate_capture_for_args

RENDER_READY_POLL_SECONDS = 0.1


class WindowFrameCapture:
    def __init__(self, ensure_process_running):
        self.ensure_process_running = ensure_process_running

    def capture_checked_window(self, args, backend, handle):
        result = backend.capture_window(handle, args.output)
        validate_capture_for_args(args, result)
        return result

    def capture_existing_handle(self, args, backend):
        return self.capture_checked_window(args, backend, args.window_handle)

    def capture_render_ready_window(self, args, backend, handle, process):
        deadline = time.monotonic() + args.render_ready_timeout
        while True:
            self.ensure_process_running(process, "while waiting for rendered window content")
            result = backend.capture_window(handle, args.output)
            if not result.appears_empty_or_white:
                validate_capture_for_args(args, result)
                return result

            self.ensure_process_running(process, "after a blank window capture")
            remaining_seconds = deadline - time.monotonic()
            if remaining_seconds <= 0.0:
                raise SmokeFailure(f"captured window 0x{result.handle:x}, but it remained blank or white for " f"{args.render_ready_timeout:.2f} seconds while waiting for rendered content")
            time.sleep(min(RENDER_READY_POLL_SECONDS, remaining_seconds))

    def capture_resized_window(self, args, backend, handle, process, log_directory, log_baseline, log_pattern):
        backend.prepare_window(handle)
        before_marker = getattr(args, "expect_before_resize_log_message", None)
        if before_marker:
            wait_for_log_message(log_directory, log_baseline, log_pattern, before_marker, args.render_ready_timeout)
            self.ensure_process_running(process, "after the pre-resize render marker")
        before_args = SimpleNamespace(**vars(args))
        before_args.output = args.output.with_name(args.output.stem + ".before-resize" + args.output.suffix)
        before = self.capture_render_ready_window(before_args, backend, handle, process)
        original_size = (before.width, before.height)
        target_size = tuple(args.resize_client)
        if original_size == target_size:
            raise SmokeFailure("--resize-client must change the original client extent")
        if backend.client_size(handle) != original_size:
            raise SmokeFailure("original client and capture extents disagree before resize")
        write_status(f"rendered original client {before.width}x{before.height}; settling {args.settle_seconds:g}s before resize")
        time.sleep(args.settle_seconds)
        self.ensure_process_running(process, "before client resize")
        if backend.client_size(handle) != original_size:
            raise SmokeFailure("client extent changed during the original-size settle interval")
        settled_before = self.capture_render_ready_window(before_args, backend, handle, process)
        if (settled_before.width, settled_before.height) != original_size or backend.client_size(handle) != original_size:
            raise SmokeFailure("settled original client/capture extent does not match the original extent")

        backend.resize_client(handle, *target_size)
        marker = f"GraphicsRuntime: Back buffer resized to {target_size[0]}x{target_size[1]}"
        deadline = time.monotonic() + args.render_ready_timeout
        while True:
            self.ensure_process_running(process, "while waiting for client and framebuffer resize")
            observed_extent = backend.client_size(handle)
            client_matches = observed_extent == target_size
            log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
            if client_matches and marker in log_text:
                break
            remaining_seconds = deadline - time.monotonic()
            if remaining_seconds <= 0.0:
                raise SmokeFailure(f"resize did not produce client extent {target_size} and runtime marker '{marker}'; observed extent={observed_extent}, marker_seen={marker in log_text}")
            time.sleep(min(RENDER_READY_POLL_SECONDS, remaining_seconds))

        write_status(f"confirmed resized client and framebuffer {target_size[0]}x{target_size[1]}; settling {args.resize_settle_seconds:g}s")
        time.sleep(args.resize_settle_seconds)
        self.ensure_process_running(process, "after resized-frame settle interval")
        result = self.capture_render_ready_window(args, backend, handle, process)
        if (result.width, result.height) != target_size or backend.client_size(handle) != target_size:
            raise SmokeFailure(f"final client/capture extent does not match requested resize {target_size}")
        return result
