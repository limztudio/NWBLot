"""Publish fixture launch handles into caller-owned state before each admission step."""
from __future__ import annotations

from dataclasses import dataclass

from window_capture_smoke import SmokeFailure

# Upper bound for the first display log marker after the fixture window appears.
DISPLAY_MARKER_TIMEOUT_SECONDS = 15.0


@dataclass
class FixtureLaunch:
    environment: dict
    backend: object
    logserver: object | None = None
    application: object | None = None
    handle: object | None = None
    log_directory: object | None = None
    log_baseline: object | None = None
    log_pattern: str | None = None


def prepare_fixture_launch(args, fixture, *, build_launch_environment, build_fixture_environment, create_capture_backend):
    if not args.executable.is_file():
        raise SmokeFailure(f"executable does not exist: {args.executable}")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    environment = build_fixture_environment(build_launch_environment(args), fixture, skin=args.skin, force_x11=True)
    return FixtureLaunch(environment, create_capture_backend())


def admit_fixture_processes(args, launch, *, launch_logserver, launch_testbed):
    launch.logserver, port, launch.log_directory, launch.log_baseline, launch.log_pattern = launch_logserver(args, args.executable, launch.environment)
    launch.application = launch_testbed(args, args.executable, launch.environment, port)
    launch.handle = launch.backend.wait_for_window(launch.application.pid, args.timeout, args.window_title)
