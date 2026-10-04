#!/usr/bin/env python3
"""ProcessLauncher: Launched-process lifecycle (stop, terminate, launch, run)."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from launcher.models import LaunchSettings, ProfileSession
from launcher import repository_windows_process
from launcher.constants import (
    APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,
    APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS,
    COMMAND_RUN,
    CWD_PREFIX,
    GPUDBG_FLAG,
    LOGSERVER_LABEL,
    LOG_PREFIX,
    MSG_KILLING,
    MSG_LAUNCHED_APP,
    MSG_LEAVING_APP,
    MSG_LEAVING_BOTH,
    MSG_MISSING_EXECUTABLE,
    MSG_MISSING_WORKDIR,
    MSG_NO_EXIT_STATUS,
    MSG_NO_PKILL,
    MSG_RUN_FORCED,
    MSG_RUN_GRACEFUL,
    MSG_RUN_REQUEST,
    MSG_STOPPED_APP,
    MSG_STOPPING,
    OPTION_RUN_SECONDS,
    PKILL_COMMAND,
    PKILL_FOLLOW_FLAG,
    PLATFORM_WINDOWS,
    PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS,
    PROFILE_LOG_READY_POLL_SECONDS,
    STOP_KIND_FORCED,
    STOP_KIND_GRACEFUL,
)


class ProcessLauncher:
    """Launched-process lifecycle (stop, terminate, launch, run)."""

    @staticmethod
    def stop_existing_process(executable: Path, platform_name: str) -> None:
        if platform_name == PLATFORM_WINDOWS:
            results = repository_windows_process.stop_processes_by_image_path(
                executable,
                APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS,
                APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,
            )
            for result in results:
                stop_kind = STOP_KIND_FORCED if result.forced else STOP_KIND_GRACEFUL
                print(MSG_STOPPED_APP.format(executable=executable, result=result, stop_kind=stop_kind), flush=True)
            return

        if shutil.which(PKILL_COMMAND) is None:
            print(MSG_NO_PKILL, flush=True)
            return

        pattern = re.escape(str(executable.resolve()))
        subprocess.run([PKILL_COMMAND, PKILL_FOLLOW_FLAG, pattern], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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
        if process is None or process.poll() is not None:
            return

        print(MSG_STOPPING.format(label=label, process=process), flush=True)
        process.terminate()
        try:
            process.wait(timeout=timeout_seconds)
            return
        except subprocess.TimeoutExpired:
            print(MSG_KILLING.format(label=label, process=process), flush=True)
            process.kill()
            process.wait()

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
    def launch_process(
        args,
        executable: Path,
        working_directory: Path,
        env: Dict[str, str],
        application_args: Sequence[str],
        profile_session: Optional[ProfileSession] = None,
        paths_validated: bool = False,
    ) -> int:
        import launcher as _facade
        if not paths_validated:
            _facade.validate_launch_paths(executable, working_directory, args.dry_run)

        if args.kill_existing and not args.dry_run:
            _facade.stop_existing_process(executable, _facade.host_platform_name())

        launch = [str(executable)]
        if args.gpudbg:
            launch.append(GPUDBG_FLAG)
        launch += _facade.profile_client_args(args, profile_session)
        launch += list(application_args)

        print("+ " + _facade.format_command(launch), flush=True)
        print(CWD_PREFIX + str(working_directory), flush=True)
        if args.dry_run:
            return 0

        process: Optional[subprocess.Popen] = None
        try:
            process = subprocess.Popen(launch, cwd=working_directory, env=env, stdout=sys.stdout, stderr=sys.stderr)
            print(MSG_LAUNCHED_APP.format(executable=executable, process=process), flush=True)
            if args.detach:
                return 0

            run_seconds = getattr(args, OPTION_RUN_SECONDS, None)
            if run_seconds is not None and run_seconds > 0.0:
                if _facade.host_platform_name() == PLATFORM_WINDOWS:
                    run_result = repository_windows_process.run_bounded_process(
                        process,
                        run_seconds,
                        APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS,
                        APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,
                    )
                    if run_result.deadline_reached:
                        print(MSG_RUN_GRACEFUL.format(run_seconds=run_seconds), flush=True)
                    if run_result.forced:
                        print(MSG_RUN_FORCED.format(executable=executable, process=process, run_result=run_result), flush=True)
                    return run_result.exit_code

                exit_code = _facade.wait_for_process_exit(process, run_seconds)
                if exit_code is not None:
                    return exit_code

                print(MSG_RUN_REQUEST.format(run_seconds=run_seconds), flush=True)
                _facade.terminate_process(process, executable.name)
                if process.returncode is None:
                    raise SystemExit(MSG_NO_EXIT_STATUS.format(executable=executable))
                return process.returncode

            return process.wait()
        except KeyboardInterrupt:
            if profile_session is not None and profile_session.process is not None:
                print(MSG_LEAVING_BOTH, flush=True)
            else:
                print(MSG_LEAVING_APP, flush=True)
            return 0
        finally:
            if (
                profile_session is not None
                and profile_session.process is not None
                and not args.detach
                and (process is None or process.poll() is not None)
            ):
                _facade.terminate_process(profile_session.process, LOGSERVER_LABEL)

    @staticmethod
    def launch_with_optional_profile(
        args,
        settings: LaunchSettings,
        executable: Path,
        working_directory: Path,
        env: Dict[str, str],
        application_args: Sequence[str],
    ) -> int:
        import launcher as _facade
        _facade.validate_launch_paths(executable, working_directory, args.dry_run)
        profile_session = _facade.start_profile_session(args, settings, working_directory, env)
        return _facade.launch_process(args, executable, working_directory, env, application_args, profile_session, paths_validated=True)

    @staticmethod
    def run_target_launcher(target: str, argv: Sequence[str]) -> int:
        import launcher as _facade
        return _facade.main([COMMAND_RUN, target] + list(argv))

    @staticmethod
    def run_repo_script(script: Path, script_args: Sequence[str], echo: bool = True) -> int:
        import launcher as _facade
        root = _facade.repo_root()
        script_path = root / script
        command = [sys.executable, str(script_path)] + list(script_args)
        if echo:
            print(LOG_PREFIX + _facade.format_command(command), flush=True)
        return subprocess.run(command, cwd=root).returncode


stop_existing_process = ProcessLauncher.stop_existing_process
validate_launch_paths = ProcessLauncher.validate_launch_paths
terminate_process = ProcessLauncher.terminate_process
wait_for_process_exit = ProcessLauncher.wait_for_process_exit
launch_process = ProcessLauncher.launch_process
launch_with_optional_profile = ProcessLauncher.launch_with_optional_profile
run_target_launcher = ProcessLauncher.run_target_launcher
run_repo_script = ProcessLauncher.run_repo_script