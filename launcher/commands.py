#!/usr/bin/env python3
"""Dispatch and list discovered repository launchers."""

from __future__ import annotations

from pathlib import Path
import sys
from typing import Dict, Sequence

from launcher.models import RepoLauncher
from launcher.discovery import LauncherDiscovery
from launcher.dispatch import DispatchRouter
from launcher.process import ProcessLauncher
from launcher.constants import (
    COMMAND_HELP_LONG,
    COMMAND_HELP_SHORT,
    COMMAND_PROFILES,
    EXIT_USAGE_ERROR,
    LIST_ITEM_SEPARATOR,
    MSG_BUILD_USAGE,
    MSG_DIR_COMMANDS,
    MSG_DIR_ITEM,
    MSG_LIST_COMMANDS,
    MSG_ROUTE_ITEM,
    MSG_RUN_USAGE,
    MSG_UNKNOWN_LAUNCHER,
    ROUTE_SEPARATOR,
)


class LauncherCommands:
    @staticmethod
    def run_discovered_launcher(repo_launcher: RepoLauncher, forwarded_args: Sequence[str], echo: bool = True) -> int:
        if repo_launcher.route:
            route_arguments = [LauncherDiscovery.launch_command_from_directory(script) for script in repo_launcher.route[1:]]
            return ProcessLauncher.run_repo_script(repo_launcher.route[0], route_arguments + [repo_launcher.command] + list(forwarded_args), echo=echo,)
        return ProcessLauncher.run_repo_script(repo_launcher.script, forwarded_args, echo=echo)

    @staticmethod
    def list_directory_launchers(directory: Path, launchers: Dict[str, RepoLauncher]) -> None:
        print(MSG_DIR_COMMANDS.format(directory=directory), flush=True)
        for launcher in launchers.values():
            print(MSG_DIR_ITEM.format(launcher=launcher), flush=True)

    @staticmethod
    def run_directory_launcher(directory: Path, argv: Sequence[str]) -> int:
        """Dispatch a category launcher to one of its leaf launchers."""
        launchers = LauncherDiscovery.discover_directory_launchers(directory)
        values = list(argv)
        if not values:
            LauncherCommands.list_directory_launchers(directory, launchers)
            return EXIT_USAGE_ERROR
        if values[0] in (COMMAND_HELP_SHORT, COMMAND_HELP_LONG, COMMAND_PROFILES):
            LauncherCommands.list_directory_launchers(directory, launchers)
            return 0

        repo_launcher = launchers.get(values[0])
        if repo_launcher is None:
            valid = LIST_ITEM_SEPARATOR.join(launchers)
            print(MSG_UNKNOWN_LAUNCHER.format(directory=directory, values=values, valid=valid), file=sys.stderr)
            return EXIT_USAGE_ERROR

        forwarded_args = values[1:]
        return ProcessLauncher.run_repo_script(repo_launcher.script, forwarded_args, echo=not DispatchRouter.is_help_request(forwarded_args))

    @staticmethod
    def list_profiles_command(args) -> int:
        print(MSG_LIST_COMMANDS, flush=True)
        print(MSG_BUILD_USAGE, flush=True)
        print(MSG_RUN_USAGE, flush=True)
        for launcher in args.repo_launchers.values():
            route = ROUTE_SEPARATOR.join(str(script) for script in (*launcher.route, launcher.script))
            print(MSG_ROUTE_ITEM.format(launcher=launcher, route=route), flush=True)
        return 0
