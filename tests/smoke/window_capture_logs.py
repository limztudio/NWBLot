"""Window-capture log plumbing: logserver port selection and readiness, log-file snapshots and deltas, and message waits."""


import socket
import time
from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure, write_status  # noqa: E402


LIT_PORT_PROBE_TIMEOUT_SECONDS = 0.2
LIT_SHORT_POLL_SECONDS = 0.05
LIT_LOG_POLL_SECONDS = 0.1
LIT_N_127_0_0_1 = "127.0.0.1"
LIT_RB = "rb"
LIT_UTF_8 = "utf-8"
LIT_REPLACE = "replace"


def choose_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((LIT_N_127_0_0_1, 0))
        return sock.getsockname()[1]


def wait_for_tcp_port(port, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(LIT_PORT_PROBE_TIMEOUT_SECONDS)
            try:
                sock.connect((LIT_N_127_0_0_1, port))
                return
            except OSError:
                time.sleep(LIT_SHORT_POLL_SECONDS)

    raise SmokeFailure(f"logserver did not open port {port}")


def snapshot_log_files(directory, pattern):
    try:
        return {path: path.stat().st_size for path in directory.glob(pattern)}
    except OSError:
        return {}


def read_log_delta(path, offset):
    try:
        size = path.stat().st_size
        if size <= offset:
            return ""

        with path.open(LIT_RB) as log_file:
            log_file.seek(offset)
            return log_file.read().decode(LIT_UTF_8, LIT_REPLACE)
    except OSError:
        return ""


def collect_log_delta(directory, baseline, pattern):
    chunks = []
    try:
        paths = sorted(directory.glob(pattern))
    except OSError:
        return ""

    for path in paths:
        chunks.append(read_log_delta(path, baseline.get(path, 0)))

    return "\n".join(chunk for chunk in chunks if chunk)


def standalone_log_pattern(executable):
    return f"{executable.resolve().stem}_*.log"


def wait_for_log_message(directory, baseline, pattern, needle, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    last_text = ""
    while time.monotonic() < deadline:
        text = collect_log_delta(directory, baseline, pattern)
        if needle in text:
            return
        if text:
            last_text = text[-4000:]
        time.sleep(LIT_LOG_POLL_SECONDS)

    raise SmokeFailure(f"timed out waiting for log message '{needle}'\n{last_text}")


def wait_for_log_drain(directory, baseline, pattern, timeout_seconds=2.0, quiet_seconds=0.25):
    if not directory:
        return

    deadline = time.monotonic() + timeout_seconds
    quiet_deadline = time.monotonic() + quiet_seconds
    previous_text = collect_log_delta(directory, baseline, pattern)
    while time.monotonic() < deadline:
        time.sleep(LIT_SHORT_POLL_SECONDS)
        current_text = collect_log_delta(directory, baseline, pattern)
        now = time.monotonic()
        if current_text != previous_text:
            previous_text = current_text
            quiet_deadline = now + quiet_seconds
        elif now >= quiet_deadline:
            return
