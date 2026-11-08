"""Terminate captured processes while retaining failure evidence."""

import platform
import subprocess

from window_capture_outcome import SmokeFailure, write_status
from window_capture_process_closer import WindowProcessCloser
from window_capture_output_reader import CaptureOutputReader
from window_capture_process_output import CapturedProcessOutput

LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS = 10.0
LIT_FORCED_EXIT_TIMEOUT_SECONDS = 5.0
LIT_WINDOWS = "Windows"
LIT_LINUX = "Linux"
LIT_NWB_OUTPUT_CAPTURE = "_nwb_output_capture"


class CaptureProcessTermination:
    @staticmethod
    def terminate_process(process, name, window_handle=None, *, preserve_capture=False, rejected_output=(), observed_output=None, observed_needles=(), graceful_exit_timeout=LIT_GRACEFUL_EXIT_TIMEOUT_SECONDS):
        if process is None:
            return None, ""

        exit_code = None
        tail = ""
        termination_error = None
        try:
            if process.poll() is None:
                # Close the X11 window gracefully so shutdown writes Name sidecars.
                host_platform = platform.system()
                if host_platform == LIT_WINDOWS:
                    try:
                        if WindowProcessCloser.request_windows_graceful_exit(process.pid):
                            try:
                                process.wait(timeout=graceful_exit_timeout)
                            except subprocess.TimeoutExpired:
                                write_status(f"{name}: did not exit after WM_CLOSE; terminating")
                    except OSError as error:
                        write_status(f"{name}: graceful WM_CLOSE failed ({error}); terminating")

                elif host_platform == LIT_LINUX and window_handle is not None:
                    if WindowProcessCloser.request_linux_graceful_exit(window_handle):
                        try:
                            process.wait(timeout=graceful_exit_timeout)
                        except subprocess.TimeoutExpired:
                            write_status(f"{name}: did not exit after X11 WM_DELETE_WINDOW; terminating")

                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=LIT_FORCED_EXIT_TIMEOUT_SECONDS)
                    except subprocess.TimeoutExpired:
                        write_status(f"{name}: did not exit after terminate; killing")
                        process.kill()
                        process.wait(timeout=LIT_FORCED_EXIT_TIMEOUT_SECONDS)
        except BaseException as error:
            termination_error = error
            raise
        finally:
            try:
                exit_code = process.poll()
            except BaseException as poll_error:
                capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
                process._nwb_output_capture = None
                CapturedProcessOutput.dispose_process_output_capture(capture, preserve=True)
                if termination_error is None or not isinstance(poll_error, Exception):
                    raise
                CapturedProcessOutput.report_secondary_failure(f"could not poll {name} during failed cleanup: {poll_error}")
            if exit_code is None and termination_error is not None:
                capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
                process._nwb_output_capture = None
                CapturedProcessOutput.dispose_process_output_capture(capture, preserve=True)
            if exit_code is not None:
                capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
                try:
                    tail = CaptureOutputReader.read_process_tail(process)
                    if capture is not None and rejected_output:
                        CaptureOutputReader.reject_process_output(capture.path, rejected_output)
                    if capture is not None and observed_output is not None and observed_needles:
                        matches = CaptureOutputReader.find_process_output(capture.path, observed_needles)
                        if matches:
                            observed_output.extend((message, capture.path) for message in matches)
                            preserve_capture = True
                except BaseException as capture_error:
                    process._nwb_output_capture = None
                    CapturedProcessOutput.dispose_process_output_capture(capture, preserve=True)
                    if termination_error is None or not isinstance(capture_error, Exception):
                        if exit_code != 0 and isinstance(capture_error, Exception):
                            raise SmokeFailure(f"{name} exited with code {exit_code}; {capture_error}") from capture_error
                        raise
                    CapturedProcessOutput.report_secondary_failure(f"could not inspect {name} output during failed cleanup: {capture_error}")
                else:
                    process._nwb_output_capture = None
                    if capture is not None and exit_code != 0:
                        tail += f"\nFull {name} output retained at '{capture.path}'"
                    CapturedProcessOutput.dispose_process_output_capture(capture, preserve=preserve_capture or termination_error is not None or exit_code != 0)

        return exit_code, tail
