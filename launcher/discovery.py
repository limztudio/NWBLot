#!/usr/bin/env python3
"""LauncherDiscovery: Repository launcher discovery and router dispatch."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple
from launcher.models import RepoLauncher
from launcher.constants import (
    COMMAND_HELP_LONG,
    COMMAND_HELP_SHORT,
    COMMAND_PROFILES,
    KIND_CATEGORY,
    KIND_DIRECTORY,
    LAUNCHER_GLOB_SUFFIX,
    LAUNCHER_SCRIPT_NAME,
    LAUNCHER_SEARCH_ROOTS,
    LAUNCH_COMMAND_NAME_HYPHEN,
    LAUNCH_COMMAND_NAME_UNDERSCORE,
    LAUNCH_COMMAND_PATTERN,
    LIST_ITEM_SEPARATOR,
    MSG_BUILD_USAGE,
    MSG_COMMAND_CONFLICT,
    MSG_DIR_COMMANDS,
    MSG_DIR_ITEM,
    MSG_DISAMBIGUATE,
    MSG_DUPLICATE_COMMAND,
    MSG_INVALID_COMMAND,
    MSG_LIST_COMMANDS,
    MSG_MISSING_LAUNCHER_PREFIX,
    MSG_ROUTE_ITEM,
    MSG_RUN_USAGE,
    MSG_UNKNOWN_LAUNCHER,
    RESERVED_LAUNCH_COMMANDS,
    ROUTE_SEPARATOR,
)


class LauncherDiscovery:
    """Repository launcher discovery and router dispatch."""

    @staticmethod
    def launch_command_from_directory(script: Path) -> str:
        return script.parent.name.lower().replace(LAUNCH_COMMAND_NAME_UNDERSCORE, LAUNCH_COMMAND_NAME_HYPHEN)

    @staticmethod
    def validate_launch_command(command: str, source: Path) -> str:
        if not re.fullmatch(LAUNCH_COMMAND_PATTERN, command):
            raise SystemExit(
                MSG_INVALID_COMMAND.format(command=command, source=source)
            )
        if command in RESERVED_LAUNCH_COMMANDS:
            raise SystemExit(MSG_COMMAND_CONFLICT.format(command=command, source=source))
        return command

    @staticmethod
    def discover_directory_launchers(directory: Path, root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
        """Discover the launchers directly below one router directory."""
        import launcher as _facade
        root = (root or _facade.repo_root()).resolve()
        search_path = directory if directory.is_absolute() else root / directory
        search_path = search_path.resolve()

        if not search_path.is_dir():
            return {}

        launchers: Dict[str, RepoLauncher] = {}
        for script in sorted(search_path.glob(LAUNCHER_GLOB_SUFFIX), key=lambda path: path.as_posix()):

            command = _facade.validate_launch_command(_facade.launch_command_from_directory(script), script)
            launcher = _facade.RepoLauncher(command, script.relative_to(root))
            existing = launchers.get(command)
            if existing is not None:
                raise SystemExit(
                    MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher)
                    + MSG_DISAMBIGUATE
                )
            launchers[command] = launcher

        return dict(sorted(launchers.items()))

    @staticmethod
    def launcher_route(search_path: Path, leaf_script: Path, root: Path) -> Tuple[Path, ...]:
        import launcher as _facade
        relative_parts = leaf_script.parent.relative_to(search_path).parts
        route: List[Path] = []
        for depth in range(len(relative_parts)):
            directory = search_path.joinpath(*relative_parts[:depth])
            script = directory / LAUNCHER_SCRIPT_NAME
            if not script.is_file():
                kind = KIND_CATEGORY if depth == 0 else KIND_DIRECTORY
                raise SystemExit(f"{MSG_MISSING_LAUNCHER_PREFIX}{kind} launcher: {script.relative_to(root)}")
            if depth > 0:
                _facade.validate_launch_command(_facade.launch_command_from_directory(script), script)
            route.append(script.relative_to(root))
        return tuple(route)

    @staticmethod
    def discover_leaf_launchers(directory: Path, root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
        """Discover runnable leaves below a category and retain their router routes.

        A ``launch.py`` with descendant launchers is a router.  A leaf has no nested
        launchers, and every directory between it and the category must provide a
        router so nested groupings cannot be bypassed.
        """
        import launcher as _facade
        root = (root or _facade.repo_root()).resolve()
        search_path = directory if directory.is_absolute() else root / directory
        search_path = search_path.resolve()

        if not search_path.is_dir():
            return {}

        scripts = sorted(search_path.rglob(LAUNCHER_SCRIPT_NAME), key=lambda path: path.as_posix())
        launchers: Dict[str, RepoLauncher] = {}
        for script in scripts:
            if any(nested_script != script for nested_script in script.parent.rglob(LAUNCHER_SCRIPT_NAME)):
                continue

            command = _facade.validate_launch_command(_facade.launch_command_from_directory(script), script)
            launcher = _facade.RepoLauncher(command, script.relative_to(root), _facade.launcher_route(search_path, script, root))
            existing = launchers.get(command)
            if existing is not None:
                raise SystemExit(
                    MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher)
                    + MSG_DISAMBIGUATE
                )
            launchers[command] = launcher

        return dict(sorted(launchers.items()))

    @staticmethod
    def discover_repo_launchers(root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
        import launcher as _facade
        root = (root or _facade.repo_root()).resolve()
        launchers: Dict[str, RepoLauncher] = {}

        for search_root in LAUNCHER_SEARCH_ROOTS:
            for command, launcher in _facade.discover_leaf_launchers(search_root, root).items():
                existing = launchers.get(command)
                if existing is not None:
                    raise SystemExit(
                        MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher)
                        + MSG_DISAMBIGUATE
                    )
                launchers[command] = launcher

        return dict(sorted(launchers.items()))

    @staticmethod
    def run_discovered_launcher(repo_launcher: RepoLauncher, forwarded_args: Sequence[str], echo: bool = True) -> int:
        import launcher as _facade
        if repo_launcher.route:
            route_arguments = [_facade.launch_command_from_directory(script) for script in repo_launcher.route[1:]]
            return _facade.run_repo_script(
                repo_launcher.route[0],
                route_arguments + [repo_launcher.command] + list(forwarded_args),
                echo=echo,
            )
        return _facade.run_repo_script(repo_launcher.script, forwarded_args, echo=echo)

    @staticmethod
    def list_directory_launchers(directory: Path, launchers: Dict[str, RepoLauncher]) -> None:
        print(MSG_DIR_COMMANDS.format(directory=directory), flush=True)
        for launcher in launchers.values():
            print(MSG_DIR_ITEM.format(launcher=launcher), flush=True)

    @staticmethod
    def run_directory_launcher(directory: Path, argv: Sequence[str]) -> int:
        """Dispatch a category launcher to one of its leaf launchers."""
        import launcher as _facade
        launchers = _facade.discover_directory_launchers(directory)
        values = list(argv)
        if not values:
            _facade.list_directory_launchers(directory, launchers)
            return 2
        if values[0] in (COMMAND_HELP_SHORT, COMMAND_HELP_LONG, COMMAND_PROFILES):
            _facade.list_directory_launchers(directory, launchers)
            return 0

        repo_launcher = launchers.get(values[0])
        if repo_launcher is None:
            valid = LIST_ITEM_SEPARATOR.join(launchers)
            print(MSG_UNKNOWN_LAUNCHER.format(directory=directory, values=values, valid=valid), file=sys.stderr)
            return 2

        forwarded_args = values[1:]
        return _facade.run_repo_script(repo_launcher.script, forwarded_args, echo=not _facade.is_help_request(forwarded_args))

    @staticmethod
    def list_profiles_command(args) -> int:
        print(MSG_LIST_COMMANDS, flush=True)
        print(MSG_BUILD_USAGE, flush=True)
        print(MSG_RUN_USAGE, flush=True)
        for launcher in args.repo_launchers.values():
            route = ROUTE_SEPARATOR.join(str(script) for script in (*launcher.route, launcher.script))
            print(MSG_ROUTE_ITEM.format(launcher=launcher, route=route), flush=True)
        return 0


launch_command_from_directory = LauncherDiscovery.launch_command_from_directory
validate_launch_command = LauncherDiscovery.validate_launch_command
discover_directory_launchers = LauncherDiscovery.discover_directory_launchers
launcher_route = LauncherDiscovery.launcher_route
discover_leaf_launchers = LauncherDiscovery.discover_leaf_launchers
discover_repo_launchers = LauncherDiscovery.discover_repo_launchers
run_discovered_launcher = LauncherDiscovery.run_discovered_launcher
list_directory_launchers = LauncherDiscovery.list_directory_launchers
run_directory_launcher = LauncherDiscovery.run_directory_launcher
list_profiles_command = LauncherDiscovery.list_profiles_command