#!/usr/bin/env python3
"""ProfileSessionController: Logserver profile session (ports, session startup, client args)."""

from __future__ import annotations

import math
import secrets
import socket
import subprocess
from pathlib import Path
from typing import Dict, Optional, Sequence
from launcher.readiness import READINESS_OPTION, ReadinessError, wait_for_logserver
from launcher.models import LaunchSettings, ProfileSession
from launcher.build import BuildController
from launcher.cmake_settings import CmakeSettings
from launcher.process import ProcessLauncher
from launcher.constants import (CWD_PREFIX, LOGSERVER_LABEL, LOG_PREFIX, MSG_LAUNCHED_LOGSERVER, MSG_PORT_BUSY, MSG_PORT_RANGE, MSG_POSITIVE_TIMEOUT,
    PROFILE_CLIENT_PORT_FLAG, PROFILE_LOG_DRY_RUN_PORT, PROFILE_LOG_HOST, PROFILE_LOG_PORT_AUTO, PROFILE_LOG_PORT_MAX, PROFILE_LOG_PORT_MIN,)


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
        port = int(args.profile_log_port)
        if port < PROFILE_LOG_PORT_MIN or port > PROFILE_LOG_PORT_MAX:
            raise SystemExit(MSG_PORT_RANGE)
        if port != PROFILE_LOG_PORT_AUTO:
            if not args.dry_run:
                ProfileSessionController.ensure_tcp_port_available(port)
            return port
        if args.dry_run:
            return PROFILE_LOG_DRY_RUN_PORT
        return ProfileSessionController.choose_free_tcp_port()

    @staticmethod
    def wait_for_tcp_port(port: int, timeout_seconds: float, process: subprocess.Popen, token: str) -> None:
        try:
            wait_for_logserver(port, token, timeout_seconds, process)
        except ReadinessError as error:
            raise SystemExit(str(error)) from error

    @staticmethod
    def start_profile_session(args, settings: LaunchSettings, working_directory: Path, env: Dict[str, str],) -> Optional[ProfileSession]:
        if not args.with_profile:
            return None

        if not math.isfinite(args.profile_logserver_timeout) or args.profile_logserver_timeout <= 0.0:
            raise SystemExit(MSG_POSITIVE_TIMEOUT)

        log_port = ProfileSessionController.resolve_profile_log_port(args)
        logserver_executable = BuildController.resolve_executable_path(settings, args.profile_logserver_target, args.profile_logserver_executable, args.profile_logserver_name, args.dry_run,)
        ProcessLauncher.validate_launch_paths(logserver_executable, working_directory, args.dry_run)

        token = secrets.token_hex(16) if not args.dry_run else ""
        command = [str(logserver_executable), PROFILE_CLIENT_PORT_FLAG, str(log_port)] + ([READINESS_OPTION, token] if token else []) + list(args.profile_logserver_args)
        print(LOG_PREFIX + CmakeSettings.format_command(command), flush=True)
        print(CWD_PREFIX + str(working_directory), flush=True)
        if args.dry_run:
            return ProfileSession(log_port, logserver_executable, None)

        process: Optional[subprocess.Popen] = None
        published = False
        try:
            process = subprocess.Popen(command, cwd=working_directory, env=env)
            print(MSG_LAUNCHED_LOGSERVER.format(logserver_executable=logserver_executable, process=process, log_port=log_port), flush=True)
            ProfileSessionController.wait_for_tcp_port(log_port, args.profile_logserver_timeout, process, token)
            session = ProfileSession(log_port, logserver_executable, process)
            published = True
            return session
        finally:
            if process is not None and not published:
                ProcessLauncher.terminate_process(process, LOGSERVER_LABEL)

    @staticmethod
    def launch_with_optional_profile(args, settings: LaunchSettings, executable: Path, working_directory: Path, env: Dict[str, str], application_args: Sequence[str],) -> int:
        ProcessLauncher.validate_launch_paths(executable, working_directory, args.dry_run)
        profile_session = ProfileSessionController.start_profile_session(args, settings, working_directory, env)
        return ProcessLauncher.launch_process(args, executable, working_directory, env, application_args, profile_session, paths_validated=True)
