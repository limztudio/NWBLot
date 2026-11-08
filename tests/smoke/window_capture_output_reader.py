"""Inspect and validate captured process output."""

import os

from window_capture_outcome import SmokeFailure

LIT_RB = "rb"
LIT_UTF_8 = "utf-8"
LIT_REPLACE = "replace"
LIT_NWB_OUTPUT_CAPTURE = "_nwb_output_capture"


class CaptureOutputReader:
    @staticmethod
    def find_process_output(path, messages):
        needles = [(value, value.encode(LIT_UTF_8)) for value in messages if value]
        if not needles:
            return set()
        overlap = max(len(value) for _, value in needles) - 1
        previous = b""
        found = set()
        try:
            with path.open(LIT_RB) as stream:
                while chunk := stream.read(65536):
                    window = previous + chunk
                    found.update(text for text, value in needles if value in window)
                    previous = window[-overlap:] if overlap else b""
        except OSError as error:
            raise SmokeFailure(f"could not read process output '{path}': {error}") from error
        return found

    @staticmethod
    def reject_process_output(path, rejected):
        found = CaptureOutputReader.find_process_output(path, rejected)
        if found:
            raise SmokeFailure(f"process output '{path}' contained forbidden message '{sorted(found)[0]}'")

    @staticmethod
    def read_file_tail(path, max_bytes=65536):
        try:
            with path.open(LIT_RB) as log_file:
                log_file.seek(0, os.SEEK_END)
                start = max(log_file.tell() - max_bytes, 0)
                log_file.seek(start)
                return log_file.read().decode(LIT_UTF_8, LIT_REPLACE)
        except OSError as error:
            raise SmokeFailure(f"could not read process output '{path}': {error}") from error

    @staticmethod
    def read_process_tail(process):
        if process is None:
            return ""

        capture = getattr(process, LIT_NWB_OUTPUT_CAPTURE, None)
        if capture is None:
            return ""

        output = CaptureOutputReader.read_file_tail(capture.path)
        if not output:
            return ""

        lines = output.splitlines()
        return "\n".join(lines[-40:])
