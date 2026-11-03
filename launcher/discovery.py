#!/usr/bin/env python3
"""LauncherDiscovery: Repository launcher discovery and router dispatch."""

from __future__ import annotations

import os
import re
import stat
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple
from launcher.models import RepoLauncher
from launcher.cmake_settings import CmakeSettings
from launcher.dispatch import DispatchRouter
from launcher.process import ProcessLauncher
from launcher.constants import (COMMAND_HELP_LONG, COMMAND_HELP_SHORT, COMMAND_PROFILES, EXIT_USAGE_ERROR, KIND_CATEGORY, KIND_DIRECTORY, LAUNCHER_ARTIFACT_DIRECTORIES, LAUNCHER_SCRIPT_NAME, LAUNCHER_SEARCH_ROOTS, LAUNCH_COMMAND_NAME_HYPHEN,
    LAUNCH_COMMAND_NAME_UNDERSCORE, LAUNCH_COMMAND_PATTERN, LIST_ITEM_SEPARATOR, MSG_BUILD_USAGE, MSG_COMMAND_CONFLICT, MSG_DIR_COMMANDS, MSG_DIR_ITEM, MSG_DISAMBIGUATE, MSG_DUPLICATE_COMMAND, MSG_INVALID_COMMAND, MSG_LIST_COMMANDS,
    MSG_MISSING_LAUNCHER_PREFIX, MSG_ROUTE_ITEM, MSG_RUN_USAGE, MSG_UNKNOWN_LAUNCHER, RESERVED_LAUNCH_COMMANDS, ROUTE_SEPARATOR,)


class LauncherDiscovery:
    """Repository launcher discovery and router dispatch."""

    @staticmethod
    def _register_launcher(launchers: Dict[str, RepoLauncher], launcher: RepoLauncher) -> None:
        command = launcher.command
        existing = launchers.get(command)
        if existing is not None:
            raise SystemExit(MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher) + MSG_DISAMBIGUATE)
        launchers[command] = launcher

    @staticmethod
    def launch_command_from_directory(script: Path) -> str:
        return script.parent.name.lower().replace(LAUNCH_COMMAND_NAME_UNDERSCORE, LAUNCH_COMMAND_NAME_HYPHEN)

    @staticmethod
    def validate_launch_command(command: str, source: Path) -> str:
        if not re.fullmatch(LAUNCH_COMMAND_PATTERN, command):
            raise SystemExit(MSG_INVALID_COMMAND.format(command=command, source=source))
        if command in RESERVED_LAUNCH_COMMANDS:
            raise SystemExit(MSG_COMMAND_CONFLICT.format(command=command, source=source))
        return command

    @staticmethod
    def _launcher_scripts(search_path: Path) -> List[Path]:
        scripts: List[Path] = []
        pending = [search_path]
        while pending:
            directory = pending.pop()
            children = []
            with os.scandir(directory) as entries:
                for entry in entries:
                    if entry.name in LAUNCHER_ARTIFACT_DIRECTORIES:
                        continue
                    if entry.is_symlink():
                        if entry.name == LAUNCHER_SCRIPT_NAME and not entry.is_dir():
                            scripts.append(Path(entry.path))
                        continue
                    if entry.is_dir():
                        children.append(Path(entry.path))
                    elif entry.name == LAUNCHER_SCRIPT_NAME:
                        scripts.append(Path(entry.path))
            pending.extend(sorted(children, key=lambda path: path.as_posix(), reverse=True))
        return sorted(scripts, key=lambda path: path.as_posix())

    @staticmethod
    def _is_directory(path: Path) -> bool:
        try:
            return stat.S_ISDIR(path.stat().st_mode)
        except (FileNotFoundError, NotADirectoryError):
            return False

    @staticmethod
    def _is_file(path: Path) -> bool:
        try:
            return stat.S_ISREG(path.stat().st_mode)
        except (FileNotFoundError, NotADirectoryError):
            return False

    @staticmethod
    def discover_directory_launchers(directory: Path, root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
        """Discover the launchers directly below one router directory."""
        root = (root or CmakeSettings.repo_root()).resolve()
        search_path = directory if directory.is_absolute() else root / directory
        search_path = search_path.resolve()

        if search_path.name in LAUNCHER_ARTIFACT_DIRECTORIES or not LauncherDiscovery._is_directory(search_path):
            return {}

        launchers: Dict[str, RepoLauncher] = {}
        scripts = []
        with os.scandir(search_path) as entries:
            for entry in entries:
                if entry.name in LAUNCHER_ARTIFACT_DIRECTORIES or not entry.is_dir():
                    continue
                script = Path(entry.path) / LAUNCHER_SCRIPT_NAME
                try:
                    script.lstat()
                except (FileNotFoundError, NotADirectoryError):
                    continue
                scripts.append(script)
        for script in sorted(scripts, key=lambda path: path.as_posix()):
            command = LauncherDiscovery.validate_launch_command(LauncherDiscovery.launch_command_from_directory(script), script)
            launcher = RepoLauncher(command, script.relative_to(root))
            LauncherDiscovery._register_launcher(launchers, launcher)

        return dict(sorted(launchers.items()))

    @staticmethod
    def launcher_route(search_path: Path, leaf_script: Path, root: Path) -> Tuple[Path, ...]:
        relative_parts = leaf_script.parent.relative_to(search_path).parts
        route: List[Path] = []
        for depth in range(len(relative_parts)):
            directory = search_path.joinpath(*relative_parts[:depth])
            script = directory / LAUNCHER_SCRIPT_NAME
            if not LauncherDiscovery._is_file(script):
                kind = KIND_CATEGORY if depth == 0 else KIND_DIRECTORY
                raise SystemExit(f"{MSG_MISSING_LAUNCHER_PREFIX}{kind} launcher: {script.relative_to(root)}")
            if depth > 0:
                LauncherDiscovery.validate_launch_command(LauncherDiscovery.launch_command_from_directory(script), script)
            route.append(script.relative_to(root))
        return tuple(route)

    @staticmethod
    def discover_leaf_launchers(directory: Path, root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
        """Discover runnable leaves below a category and retain their router routes.

        A ``launch.py`` with descendant launchers is a router.  A leaf has no nested
        launchers, and every directory between it and the category must provide a
        router so nested groupings cannot be bypassed.
        """
        root = (root or CmakeSettings.repo_root()).resolve()
        search_path = directory if directory.is_absolute() else root / directory
        search_path = search_path.resolve()

        if search_path.name in LAUNCHER_ARTIFACT_DIRECTORIES or not LauncherDiscovery._is_directory(search_path):
            return {}

        scripts = LauncherDiscovery._launcher_scripts(search_path)
        script_directories = {script.parent for script in scripts}
        router_directories = set()
        for script in scripts:
            for ancestor in script.parent.parents:
                if ancestor in script_directories:
                    router_directories.add(ancestor)
                if ancestor == search_path:
                    break
        launchers: Dict[str, RepoLauncher] = {}
        for script in scripts:
            if script.parent in router_directories:
                continue

            command = LauncherDiscovery.validate_launch_command(LauncherDiscovery.launch_command_from_directory(script), script)
            launcher = RepoLauncher(command, script.relative_to(root), LauncherDiscovery.launcher_route(search_path, script, root))
            LauncherDiscovery._register_launcher(launchers, launcher)

        return dict(sorted(launchers.items()))

    @staticmethod
    def discover_repo_launchers(root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
        root = (root or CmakeSettings.repo_root()).resolve()
        launchers: Dict[str, RepoLauncher] = {}

        for search_root in LAUNCHER_SEARCH_ROOTS:
            for launcher in LauncherDiscovery.discover_leaf_launchers(search_root, root).values():
                LauncherDiscovery._register_launcher(launchers, launcher)

        return dict(sorted(launchers.items()))

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
            LauncherDiscovery.list_directory_launchers(directory, launchers)
            return EXIT_USAGE_ERROR
        if values[0] in (COMMAND_HELP_SHORT, COMMAND_HELP_LONG, COMMAND_PROFILES):
            LauncherDiscovery.list_directory_launchers(directory, launchers)
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
