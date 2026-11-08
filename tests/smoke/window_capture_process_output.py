"""Own temporary process-output captures and their launch lifecycle."""

from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys
import tempfile

from window_capture_outcome import SmokeFailure, write_status


@dataclass
class ProcessOutputCapture:
    path: Path
    stream: object


class CapturedProcessOutput:
    @staticmethod
    def create_process_output_capture(working_directory, name):
        try:
            stream = tempfile.NamedTemporaryFile(mode="w+b", prefix=f".nwb_window_capture_{name}_", suffix=".log", dir=working_directory, delete=False,)
        except OSError as error:
            raise SmokeFailure(f"failed to create {name} output capture in '{working_directory}': {error}") from error

        return ProcessOutputCapture(Path(stream.name), stream)

    @staticmethod
    def report_secondary_failure(message):
        try:
            write_status(message)
        except Exception:
            pass

    @staticmethod
    def dispose_process_output_capture(capture, *, preserve=False):
        if capture is None:
            return

        primary_error = sys.exc_info()[1]
        try:
            capture.stream.close()
        except OSError as error:
            message = f"failed to close process output capture '{capture.path}': {error}"
            if primary_error is None:
                raise SmokeFailure(message) from error
            CapturedProcessOutput.report_secondary_failure(message)
            return

        if preserve:
            return

        try:
            capture.path.unlink()
        except FileNotFoundError:
            pass
        except OSError as error:
            message = f"failed to remove process output capture '{capture.path}': {error}"
            if primary_error is None:
                raise SmokeFailure(message) from error
            CapturedProcessOutput.report_secondary_failure(message)

    @staticmethod
    def launch_captured_process(command, working_directory, env, name):
        capture = CapturedProcessOutput.create_process_output_capture(working_directory, name)
        try:
            process = subprocess.Popen(command, cwd=working_directory, env=env, stdout=capture.stream, stderr=subprocess.STDOUT,)
        except OSError:
            CapturedProcessOutput.dispose_process_output_capture(capture)
            raise

        process._nwb_output_capture = capture
        return process
