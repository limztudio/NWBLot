#!/usr/bin/env python3
"""ProfileSessionController: Logserver profile session (ports, session startup, client args)."""

from __future__ import annotations

import socket
import subprocess
import time
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from launcher.models import LaunchSettings, ProfileSession
from launcher.constants import (
    CWD_PREFIX,
    EMPTY_STRING,
    LOGSERVER_LABEL,
    LOG_PREFIX,
    MSG_LAUNCHED_LOGSERVER,
    MSG_LOGSERVER_EXITED,
    MSG_LOGSERVER_NO_ACCEPT,
    MSG_LOGSERVER_TIMEOUT_SUFFIX,
    MSG_PORT_BUSY,
    MSG_PORT_RANGE,
    MSG_POSITIVE_TIMEOUT,
    OPTION_WITH_PROFILE,
    PROFILE_CLIENT_ADDRESS_FLAG,
    PROFILE_CLIENT_PORT_FLAG,
    PROFILE_LOG_CONNECT_TIMEOUT_SECONDS,
    PROFILE_LOG_DRY_RUN_PORT,
    PROFILE_LOG_HOST,
    PROFILE_LOG_PORT_AUTO,
    PROFILE_LOG_PORT_MAX,
    PROFILE_LOG_PORT_MIN,
    PROFILE_LOG_READY_POLL_SECONDS,
)


class ProfileSessionController:
    """Logserver profile session (ports, session startup, client args)."""

    @staticmethod
    def choose_free_tcp_port() -> int:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind((PROFILE_LOG_HOST, PROFILE_LOG_PORT_AUTO))
            return int(listener.getsockname()[1])

    @staticmethod
    def ensure_tcp_port_available(port: int) -> None:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            try:
                listener.bind((PROFILE_LOG_HOST, port))
            except OSError as exc:
                raise SystemExit(MSG_PORT_BUSY.format(port=port, exc=exc)) from exc

    @staticmethod
    def resolve_profile_log_port(args) -> int:
        import launcher as _facade
        port = int(args.profile_log_port)
        if port < PROFILE_LOG_PORT_MIN or port > PROFILE_LOG_PORT_MAX:
            raise SystemExit(MSG_PORT_RANGE)
        if port != PROFILE_LOG_PORT_AUTO:
            if not args.dry_run:
                _facade.ensure_tcp_port_available(port)
            return port
        if args.dry_run:
            return PROFILE_LOG_DRY_RUN_PORT
        return _facade.choose_free_tcp_port()

    @staticmethod
    def wait_for_tcp_port(port: int, timeout_seconds: float, process: Optional[subprocess.Popen]) -> None:
        deadline = time.monotonic() + timeout_seconds
        last_error: Optional[OSError] = None
        while time.monotonic() < deadline:
            if process is not None and process.poll() is not None:
                raise SystemExit(MSG_LOGSERVER_EXITED.format(port=port, process=process))

            try:
                with socket.create_connection((PROFILE_LOG_HOST, port), timeout=PROFILE_LOG_CONNECT_TIMEOUT_SECONDS):
                    return
            except OSError as exc:
                last_error = exc
                time.sleep(PROFILE_LOG_READY_POLL_SECONDS)

        suffix = MSG_LOGSERVER_TIMEOUT_SUFFIX.format(last_error=last_error) if last_error is not None else EMPTY_STRING
        raise SystemExit(MSG_LOGSERVER_NO_ACCEPT.format(port=port, timeout_seconds=timeout_seconds, suffix=suffix))

    @staticmethod
    def profile_client_args(args, profile_session: Optional[ProfileSession]) -> List[str]:
        if profile_session is None:
            return []
        return [PROFILE_CLIENT_ADDRESS_FLAG, args.profile_log_address, PROFILE_CLIENT_PORT_FLAG, str(profile_session.log_port)]

    @staticmethod
    def start_profile_session(
        args,
        settings: LaunchSettings,
        working_directory: Path,
        env: Dict[str, str],
    ) -> Optional[ProfileSession]:
        import launcher as _facade
        if not getattr(args, OPTION_WITH_PROFILE, False):
            return None

        if args.profile_logserver_timeout <= 0.0:
            raise SystemExit(MSG_POSITIVE_TIMEOUT)

        log_port = _facade.resolve_profile_log_port(args)
        logserver_executable = _facade.resolve_executable_path(
            settings,
            args.profile_logserver_target,
            args.profile_logserver_executable,
            args.profile_logserver_name,
            args.dry_run,
        )
        _facade.validate_launch_paths(logserver_executable, working_directory, args.dry_run)

        command = [str(logserver_executable), PROFILE_CLIENT_PORT_FLAG, str(log_port)] + list(args.profile_logserver_args)
        print(LOG_PREFIX + _facade.format_command(command), flush=True)
        print(CWD_PREFIX + str(working_directory), flush=True)
        if args.dry_run:
            return _facade.ProfileSession(log_port, logserver_executable, None)

        process = subprocess.Popen(command, cwd=working_directory, env=env)
        print(MSG_LAUNCHED_LOGSERVER.format(logserver_executable=logserver_executable, process=process, log_port=log_port), flush=True)
        try:
            _facade.wait_for_tcp_port(log_port, args.profile_logserver_timeout, process)
        except BaseException:
            _facade.terminate_process(process, LOGSERVER_LABEL)
            raise

        return _facade.ProfileSession(log_port, logserver_executable, process)


choose_free_tcp_port = ProfileSessionController.choose_free_tcp_port
ensure_tcp_port_available = ProfileSessionController.ensure_tcp_port_available
resolve_profile_log_port = ProfileSessionController.resolve_profile_log_port
wait_for_tcp_port = ProfileSessionController.wait_for_tcp_port
profile_client_args = ProfileSessionController.profile_client_args
start_profile_session = ProfileSessionController.start_profile_session