"""Window-capture log plumbing: logserver port selection and readiness, log-file snapshots and deltas, and message waits."""


import fnmatch
import hashlib
import os
import socket
import sys
from pathlib import Path
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from launcher.readiness import READINESS_OPTION, ReadinessError, wait_for_logserver  # noqa: E402
from window_capture_outcome import SmokeFailure  # noqa: E402


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


def wait_for_tcp_port(port, timeout_seconds, process, token):
    try:
        wait_for_logserver(port, token, timeout_seconds, process)
    except ReadinessError as error:
        raise SmokeFailure(str(error)) from error

def matching_log_files(directory, pattern):
    with os.scandir(directory) as entries:
        return sorted(directory / entry.name for entry in entries if fnmatch.fnmatch(entry.name, pattern))


def _file_identity(info):
    return info.st_dev, info.st_ino


def _hash_prefix(stream, size):
    digest = hashlib.sha256()
    remaining = size
    while remaining:
        chunk = stream.read(min(remaining, 1024 * 1024))
        if not chunk:
            raise SmokeFailure("log file shrank while reading its baseline")
        digest.update(chunk)
        remaining -= len(chunk)
    return digest.digest()


def snapshot_log_files(directory, pattern):
    """Snapshot append-only logs; an identical truncate-and-rewrite is not observable."""
    baseline = {}
    for path in matching_log_files(directory, pattern):
        with path.open(LIT_RB) as log_file:
            info = os.fstat(log_file.fileno())
            baseline[path] = (_file_identity(info), info.st_size, _hash_prefix(log_file, info.st_size))
    return baseline


class ScopedLogBaseline(dict):
    def __init__(self, baseline, pattern, pid):
        self.full_baseline = baseline
        self.full_pattern = pattern
        self.live_pattern = f"logserver_????????_??????_{pid}_*.log"
        super().__init__((path, entry) for path, entry in baseline.items() if fnmatch.fnmatch(path.name, self.live_pattern))


def full_log_scope(baseline, pattern):
    if isinstance(baseline, ScopedLogBaseline):
        return baseline.full_baseline, baseline.full_pattern
    return baseline, pattern


def read_log_delta(path, baseline):
    with path.open(LIT_RB) as log_file:
        info = os.fstat(log_file.fileno())
        identity = _file_identity(info)
        if baseline is None:
            content = log_file.read()
            log_file.seek(0)
            if _hash_prefix(log_file, len(content)) != hashlib.sha256(content).digest():
                raise SmokeFailure(f"log file changed while it was read: '{path}'")
        else:
            expected_identity, offset, digest = baseline
            if identity != expected_identity:
                raise SmokeFailure(f"log file was replaced after its baseline was captured: '{path}'")
            if info.st_size < offset:
                raise SmokeFailure(f"log file shrank after its baseline was captured: '{path}'")
            if _hash_prefix(log_file, offset) != digest:
                raise SmokeFailure(f"log file changed before its baseline offset: '{path}'")
            content = log_file.read()
            log_file.seek(offset)
            if _hash_prefix(log_file, len(content)) != hashlib.sha256(content).digest():
                raise SmokeFailure(f"log file changed while it was read: '{path}'")
            log_file.seek(0)
            if _hash_prefix(log_file, offset) != digest:
                raise SmokeFailure(f"log file changed while it was read: '{path}'")
        if _file_identity(os.fstat(log_file.fileno())) != identity or _file_identity(path.stat()) != identity:
            raise SmokeFailure(f"log file was replaced while it was read: '{path}'")
        return content.decode(LIT_UTF_8, LIT_REPLACE)

def collect_log_delta(directory, baseline, pattern):
    paths = matching_log_files(directory, pattern)
    missing = baseline.keys() - set(paths)
    if missing:
        raise SmokeFailure(f"log file disappeared after its baseline was captured: '{next(iter(missing))}'")
    chunks = [read_log_delta(path, baseline.get(path)) for path in paths]
    return "\n".join(chunk for chunk in chunks if chunk)

def standalone_log_pattern(executable):
    return f"{executable.resolve().stem}_*.log"


LOG_TAIL_CHAR_LIMIT = 4000


def wait_for_log_message(directory, baseline, pattern, needle, timeout_seconds):
    deadline = time.monotonic() + timeout_seconds
    last_text = ""
    while time.monotonic() < deadline:
        text = collect_log_delta(directory, baseline, pattern)
        if needle in text:
            return
        if text:
            last_text = text[-LOG_TAIL_CHAR_LIMIT:]
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
