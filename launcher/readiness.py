"""Verify that the listener belongs to the launched logserver."""


import socket
import time


READINESS_OPTION = "--readiness-token"
MAX_HEADER_BYTES = 8192
POLL_SECONDS = 0.2


class ReadinessError(RuntimeError):
    pass


def _remaining(deadline, process):
    if process.poll() is not None:
        raise ReadinessError(f"logserver exited during readiness (exit {process.returncode})")
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise ReadinessError("logserver readiness timed out")
    return remaining


def _request_identity(port, expected, deadline, process):
    with socket.create_connection(("127.0.0.1", port), timeout=min(POLL_SECONDS, _remaining(deadline, process))) as connection:
        connection.settimeout(min(POLL_SECONDS, _remaining(deadline, process)))
        connection.sendall(b"GET /ready HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")
        received = b""
        while b"\r\n\r\n" not in received:
            connection.settimeout(min(POLL_SECONDS, _remaining(deadline, process)))
            try:
                chunk = connection.recv(1024)
            except socket.timeout:
                continue
            if not chunk:
                raise ReadinessError(f"port {port} closed an incomplete readiness response")
            received += chunk
            if len(received) > MAX_HEADER_BYTES + len(expected):
                raise ReadinessError(f"port {port} returned an oversized readiness response")
        header, body = received.split(b"\r\n\r\n", 1)
        if len(header) > MAX_HEADER_BYTES:
            raise ReadinessError(f"port {port} returned oversized readiness headers")
        lines = header.split(b"\r\n")
        status = lines[0].split()
        lengths = [line.partition(b":")[2].strip() for line in lines[1:] if line.partition(b":")[0].strip().lower() == b"content-length"]
        if len(status) < 2 or status[1] != b"200" or lengths != [str(len(expected)).encode("ascii")]:
            raise ReadinessError(f"port {port} answered with a different readiness identity")
        while len(body) < len(expected):
            connection.settimeout(min(POLL_SECONDS, _remaining(deadline, process)))
            try:
                chunk = connection.recv(len(expected) - len(body))
            except socket.timeout:
                continue
            if not chunk:
                raise ReadinessError(f"port {port} closed an incomplete readiness response")
            body += chunk
        if body != expected:
            raise ReadinessError(f"port {port} answered with a different readiness identity")
        _remaining(deadline, process)


def wait_for_logserver(port, token, timeout_seconds, process):
    deadline = time.monotonic() + timeout_seconds
    expected = token.encode("ascii")
    while True:
        _remaining(deadline, process)
        try:
            _request_identity(port, expected, deadline, process)
            return
        except OSError:
            pass
        remaining = _remaining(deadline, process)
        time.sleep(min(0.05, remaining))
