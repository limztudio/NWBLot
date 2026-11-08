#!/usr/bin/env python3
"""ProcessLauncher: Launched-process lifecycle (stop, terminate, launch, run)."""

from __future__ import annotations

import math
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence
from launcher.models import ProfileSession
from launcher import repository_windows_process
from launcher.cmake_settings import CmakeSettings
from launcher.host import HostProbe
from launcher.linux_process_stopper import LinuxProcessStopper
from launcher.constants import (APPLICATION_FORCED_STOP_TIMEOUT_SECONDS, APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS, CWD_PREFIX, GPUDBG_FLAG, LOGSERVER_LABEL, LOG_PREFIX, MSG_KILLING, MSG_LAUNCHED_APP, MSG_LEAVING_APP, MSG_LEAVING_BOTH,
    MSG_MISSING_EXECUTABLE, MSG_MISSING_WORKDIR, MSG_NO_EXIT_STATUS, MSG_POSITIVE_RUN_SECONDS, MSG_RUN_FORCED, MSG_RUN_GRACEFUL, MSG_RUN_REQUEST, MSG_STOPPED_APP, MSG_STOPPING, PLATFORM_LINUX, PLATFORM_WINDOWS,
    PROFILE_CLIENT_ADDRESS_FLAG, PROFILE_CLIENT_PORT_FLAG, PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS, PROFILE_LOG_READY_POLL_SECONDS, STOP_KIND_FORCED, STOP_KIND_GRACEFUL,)




class ProcessLauncher:
    """Launched-process lifecycle (stop, terminate, launch, run)."""

    @staticmethod
    def stop_existing_process(executable: Path, platform_name: str) -> None:
        if platform_name == PLATFORM_WINDOWS:
            results = repository_windows_process.stop_processes_by_image_path(executable, APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS, APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,)
            for result in results:
                stop_kind = STOP_KIND_FORCED if result.forced else STOP_KIND_GRACEFUL
                print(MSG_STOPPED_APP.format(executable=executable, result=result, stop_kind=stop_kind), flush=True)
            return

        LinuxProcessStopper.stop_existing_process(executable, platform_name)

    @staticmethod
    def validate_launch_paths(executable: Path, working_directory: Path, dry_run: bool) -> None:
        if dry_run:
            return
        if not executable.exists():
            raise SystemExit(MSG_MISSING_EXECUTABLE.format(executable=executable))
        if not working_directory.exists():
            raise SystemExit(MSG_MISSING_WORKDIR.format(working_directory=working_directory))

    @staticmethod
    def terminate_process(process: Optional[subprocess.Popen], label: str, timeout_seconds: float = PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS) -> None:
        primary_error = sys.exc_info()[1]
        announcement_error = None

        def report_cleanup_error(error):
            try:
                print(f"failed to stop {label}: {error}", file=sys.stderr, flush=True)
            except Exception:
                pass

        try:
            if process is None or process.poll() is not None:
                return
            if primary_error is None:
                try:
                    print(MSG_STOPPING.format(label=label, process=process), flush=True)
                except Exception as error:
                    announcement_error = error
            process.terminate()
            try:
                process.wait(timeout=timeout_seconds)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                if primary_error is None and announcement_error is None:
                    print(MSG_KILLING.format(label=label, process=process), flush=True)
        except Exception as cleanup_error:
            if primary_error is None and announcement_error is None:
                raise
            report_cleanup_error(cleanup_error)
        if announcement_error is not None:
            if primary_error is None:
                raise announcement_error
            report_cleanup_error(announcement_error)

    @staticmethod
    def wait_for_process_exit(process: subprocess.Popen, timeout_seconds: float) -> Optional[int]:
        deadline = time.monotonic() + timeout_seconds
        while True:
            exit_code = process.poll()
            if exit_code is not None:
                return exit_code

            remaining_seconds = deadline - time.monotonic()
            if remaining_seconds <= 0.0:
                return None
            time.sleep(min(PROFILE_LOG_READY_POLL_SECONDS, remaining_seconds))

    @staticmethod
    def profile_client_args(args, profile_session: Optional[ProfileSession]) -> List[str]:
        if profile_session is None:
            return []
        return [PROFILE_CLIENT_ADDRESS_FLAG, args.profile_log_address, PROFILE_CLIENT_PORT_FLAG, str(profile_session.log_port)]

    @staticmethod
    def launch_process(args, executable: Path, working_directory: Path, env: Dict[str, str], application_args: Sequence[str], profile_session: Optional[ProfileSession] = None, paths_validated: bool = False,) -> int:
        process: Optional[subprocess.Popen] = None
        retain_processes = False
        process_completed = False
        try:
            run_seconds = getattr(args, "run_seconds", None)
            if run_seconds is not None and (not math.isfinite(run_seconds) or run_seconds <= 0.0):
                raise SystemExit(MSG_POSITIVE_RUN_SECONDS)
            if run_seconds is not None and args.detach:
                raise SystemExit("--detach cannot be combined with --run-seconds")
            if not paths_validated:
                ProcessLauncher.validate_launch_paths(executable, working_directory, args.dry_run)

            if args.kill_existing and not args.dry_run:
                ProcessLauncher.stop_existing_process(executable, HostProbe.host_platform_name())

            launch = [str(executable)]
            if args.gpudbg:
                launch.append(GPUDBG_FLAG)
            launch += ProcessLauncher.profile_client_args(args, profile_session)
            launch += list(application_args)

            print("+ " + CmakeSettings.format_command(launch), flush=True)
            print(CWD_PREFIX + str(working_directory), flush=True)
            if args.dry_run:
                return 0

            process = subprocess.Popen(launch, cwd=working_directory, env=env, stdout=sys.stdout, stderr=sys.stderr)
            print(MSG_LAUNCHED_APP.format(executable=executable, process=process), flush=True)
            if args.detach:
                retain_processes = True
                return 0

            if run_seconds is not None:
                if HostProbe.host_platform_name() == PLATFORM_WINDOWS:
                    run_result = repository_windows_process.run_bounded_process(process, run_seconds, APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS, APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,)
                    process_completed = True
                    if run_result.deadline_reached:
                        print(MSG_RUN_GRACEFUL.format(run_seconds=run_seconds), flush=True)
                    if run_result.forced:
                        print(MSG_RUN_FORCED.format(executable=executable, process=process, run_result=run_result), flush=True)
                    return run_result.exit_code

                exit_code = ProcessLauncher.wait_for_process_exit(process, run_seconds)
                if exit_code is not None:
                    return exit_code

                print(MSG_RUN_REQUEST.format(run_seconds=run_seconds), flush=True)
                ProcessLauncher.terminate_process(process, executable.name)
                if process.returncode is None:
                    raise SystemExit(MSG_NO_EXIT_STATUS.format(executable=executable))
                return process.returncode

            return process.wait()
        except KeyboardInterrupt:
            retain_processes = process is not None and (args.detach or process.poll() is None)
            if profile_session is not None and profile_session.process is not None:
                print(MSG_LEAVING_BOTH, flush=True)
            else:
                print(MSG_LEAVING_APP, flush=True)
            return 0
        finally:
            try:
                if process is not None and not retain_processes and not process_completed:
                    ProcessLauncher.terminate_process(process, executable.name)
            finally:
                if profile_session is not None and profile_session.process is not None and not retain_processes:
                    ProcessLauncher.terminate_process(profile_session.process, LOGSERVER_LABEL)

    @staticmethod
    def run_repo_script(script: Path, script_args: Sequence[str], echo: bool = True) -> int:
        root = CmakeSettings.repo_root()
        script_path = root / script
        command = [sys.executable, str(script_path)] + list(script_args)
        if echo:
            print(LOG_PREFIX + CmakeSettings.format_command(command), flush=True)
        return subprocess.run(command, cwd=root).returncode
