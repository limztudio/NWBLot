#!/usr/bin/env python3
"""LauncherCli: Argument-parser construction and top-level entry routing."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from launcher.models import RepoLauncher
from launcher.constants import (
    ARG_ARCH,
    ARG_BUILD_DIR,
    ARG_BUILD_ONLY,
    ARG_CMAKE,
    ARG_COMMAND,
    ARG_CONFIGURE,
    ARG_CONFIGURE_PRESET,
    ARG_DEFINE_DEST,
    ARG_DEFINE_LONG,
    ARG_DEFINE_SHORT,
    ARG_DETACH,
    ARG_DOMAIN,
    ARG_DRY_RUN,
    ARG_EXECUTABLE,
    ARG_EXECUTABLE_NAME,
    ARG_GPUDbg,
    ARG_JOBS,
    ARG_KILL_EXISTING,
    ARG_PLATFORM,
    ARG_PROFILE_LOGSERVER_ARG,
    ARG_PROFILE_LOGSERVER_ARGS_DEST,
    ARG_PROFILE_LOGSERVER_EXECUTABLE,
    ARG_PROFILE_LOGSERVER_NAME,
    ARG_PROFILE_LOGSERVER_TARGET,
    ARG_PROFILE_LOGSERVER_TIMEOUT,
    ARG_PROFILE_LOG_ADDRESS,
    ARG_PROFILE_LOG_PORT,
    ARG_REPO_ROOT,
    ARG_RUN_SECONDS,
    ARG_SKIP_BUILD,
    ARG_TARGET,
    ARG_TARGETS,
    ARG_WITH_PROFILE,
    ARG_WORKING_DIRECTORY,
    CMAKE_CONFIG_FLAG,
    COMMAND_BUILD,
    COMMAND_PROFILES,
    COMMAND_RUN,
    COMMAND_SEPARATOR,
    CONFIGURATIONS,
    CONFIGURE_AUTO,
    CONFIGURE_CHOICES,
    DEFAULT_BUILD_JOBS,
    DEFAULT_CONFIG,
    DEFINE_ACTION,
    DEFINE_METAVAR,
    HELP_ARCH,
    HELP_BUILD_DIR,
    HELP_BUILD_DRY_RUN,
    HELP_BUILD_ONLY,
    HELP_CMAKE,
    HELP_CONFIGURE,
    HELP_CONFIGURE_PRESET,
    HELP_DETACH,
    HELP_DOMAIN,
    HELP_DRY_RUN,
    HELP_EXECUTABLE,
    HELP_EXECUTABLE_NAME,
    HELP_GPUDbg,
    HELP_JOBS,
    HELP_KILL_EXISTING,
    HELP_PLATFORM,
    HELP_PROFILE_LOGSERVER_ARG,
    HELP_PROFILE_LOGSERVER_EXECUTABLE,
    HELP_PROFILE_LOGSERVER_NAME,
    HELP_PROFILE_LOGSERVER_TARGET,
    HELP_PROFILE_LOGSERVER_TIMEOUT,
    HELP_PROFILE_LOG_ADDRESS,
    HELP_PROFILE_LOG_PORT,
    HELP_REPO_ROOT,
    HELP_RUN_SECONDS,
    HELP_SKIP_BUILD,
    HELP_WITH_PROFILE,
    HELP_WORKING_DIRECTORY,
    MAIN_ENTRY,
    MSG_BUILD_APPLICATION_ARGS,
    MSG_BUILD_ONLY_SKIP_BUILD,
    MSG_BUILD_TARGETS_HELP,
    MSG_BUILD_TARGET_HELP,
    MSG_FORWARD_THROUGH,
    MSG_LAUNCHER_DESC,
    MSG_PROFILES_HELP,
    MSG_RUN_TARGET_HELP,
    MSG_TARGET_HELP,
    NARGS_ONE_OR_MORE,
    PROFILE_LOGSERVER_EXECUTABLE,
    PROFILE_LOGSERVER_TARGET,
    PROFILE_LOGSERVER_TIMEOUT_SECONDS,
    PROFILE_LOG_ADDRESS,
    PROFILE_LOG_PORT_AUTO,
    ROUTE_SEPARATOR,
    STORE_TRUE,
    SUPPORTED_ARCHITECTURES,
)


class LauncherCli:
    """Argument-parser construction and top-level entry routing."""

    @staticmethod
    def add_build_options(parser: argparse.ArgumentParser, *, allow_skip_build: bool = True) -> None:
        import launcher as _facade
        parser.add_argument(ARG_REPO_ROOT, type=Path, help=HELP_REPO_ROOT)
        parser.add_argument(ARG_PLATFORM, default=_facade.host_platform_name(), help=HELP_PLATFORM)
        parser.add_argument(
            ARG_ARCH,
            choices=SUPPORTED_ARCHITECTURES,
            help=HELP_ARCH,
        )
        parser.add_argument(ARG_DOMAIN, help=HELP_DOMAIN)
        parser.add_argument(ARG_CONFIGURE_PRESET, help=HELP_CONFIGURE_PRESET)
        parser.add_argument(ARG_BUILD_DIR, type=Path, help=HELP_BUILD_DIR)
        parser.add_argument(ARG_CMAKE, type=Path, help=HELP_CMAKE)
        parser.add_argument(CMAKE_CONFIG_FLAG, choices=CONFIGURATIONS, default=DEFAULT_CONFIG)
        parser.add_argument(ARG_JOBS, default=DEFAULT_BUILD_JOBS, help=HELP_JOBS)
        parser.add_argument(
            ARG_CONFIGURE,
            choices=CONFIGURE_CHOICES,
            default=CONFIGURE_AUTO,
            help=HELP_CONFIGURE,
        )
        parser.add_argument(ARG_DEFINE_SHORT, ARG_DEFINE_LONG, dest=ARG_DEFINE_DEST, action=DEFINE_ACTION, default=list(), metavar=DEFINE_METAVAR)
        if allow_skip_build:
            parser.add_argument(ARG_SKIP_BUILD, action=STORE_TRUE, help=HELP_SKIP_BUILD)
        parser.add_argument(ARG_DRY_RUN, action=STORE_TRUE, help=HELP_DRY_RUN if allow_skip_build else HELP_BUILD_DRY_RUN)

    @staticmethod
    def add_common_options(parser: argparse.ArgumentParser) -> None:
        import launcher as _facade
        _facade.add_build_options(parser)
        parser.add_argument(ARG_WORKING_DIRECTORY, type=Path, help=HELP_WORKING_DIRECTORY)
        parser.add_argument(ARG_EXECUTABLE, type=Path, help=HELP_EXECUTABLE)
        parser.add_argument(ARG_EXECUTABLE_NAME, help=HELP_EXECUTABLE_NAME)
        parser.add_argument(ARG_GPUDbg, action=STORE_TRUE, help=HELP_GPUDbg)
        parser.add_argument(
            ARG_KILL_EXISTING,
            action=STORE_TRUE,
            help=HELP_KILL_EXISTING,
        )
        parser.add_argument(ARG_DETACH, action=STORE_TRUE, help=HELP_DETACH)
        parser.add_argument(
            ARG_RUN_SECONDS,
            type=float,
            default=None,
            help=HELP_RUN_SECONDS,
        )
        parser.add_argument(ARG_WITH_PROFILE, action=STORE_TRUE, help=HELP_WITH_PROFILE)
        parser.add_argument(
            ARG_PROFILE_LOG_ADDRESS,
            default=PROFILE_LOG_ADDRESS,
            help=HELP_PROFILE_LOG_ADDRESS,
        )
        parser.add_argument(
            ARG_PROFILE_LOG_PORT,
            type=int,
            default=PROFILE_LOG_PORT_AUTO,
            help=HELP_PROFILE_LOG_PORT,
        )
        parser.add_argument(
            ARG_PROFILE_LOGSERVER_TARGET,
            default=PROFILE_LOGSERVER_TARGET,
            help=HELP_PROFILE_LOGSERVER_TARGET,
        )
        parser.add_argument(
            ARG_PROFILE_LOGSERVER_NAME,
            default=PROFILE_LOGSERVER_EXECUTABLE,
            help=HELP_PROFILE_LOGSERVER_NAME,
        )
        parser.add_argument(
            ARG_PROFILE_LOGSERVER_EXECUTABLE,
            type=Path,
            help=HELP_PROFILE_LOGSERVER_EXECUTABLE,
        )
        parser.add_argument(
            ARG_PROFILE_LOGSERVER_TIMEOUT,
            type=float,
            default=PROFILE_LOGSERVER_TIMEOUT_SECONDS,
            help=HELP_PROFILE_LOGSERVER_TIMEOUT,
        )
        parser.add_argument(
            ARG_PROFILE_LOGSERVER_ARG,
            dest=ARG_PROFILE_LOGSERVER_ARGS_DEST,
            action=DEFINE_ACTION,
            default=list(),
            help=HELP_PROFILE_LOGSERVER_ARG,
        )

    @staticmethod
    def make_parser(repo_launchers: Optional[Dict[str, RepoLauncher]] = None) -> argparse.ArgumentParser:
        import launcher as _facade
        if repo_launchers is None:
            repo_launchers = _facade.discover_repo_launchers()
        parser = argparse.ArgumentParser(description=MSG_LAUNCHER_DESC)
        subparsers = parser.add_subparsers(dest=ARG_COMMAND, required=True)

        build_parser = subparsers.add_parser(COMMAND_BUILD, help=MSG_BUILD_TARGET_HELP)
        _facade.add_build_options(build_parser, allow_skip_build=False)
        build_parser.add_argument(ARG_TARGETS, nargs=NARGS_ONE_OR_MORE, help=MSG_BUILD_TARGETS_HELP)
        build_parser.set_defaults(handler=_facade.build_command, skip_build=False)

        run_parser = subparsers.add_parser(COMMAND_RUN, help=MSG_RUN_TARGET_HELP)
        _facade.add_common_options(run_parser)
        run_parser.add_argument(ARG_BUILD_ONLY, action=STORE_TRUE, help=HELP_BUILD_ONLY)
        run_parser.add_argument(ARG_TARGET, help=MSG_TARGET_HELP)
        run_parser.set_defaults(handler=_facade.run_target_command)

        for launcher in repo_launchers.values():
            route = ROUTE_SEPARATOR.join(str(script) for script in (*launcher.route, launcher.script))
            subparsers.add_parser(launcher.command, help=MSG_FORWARD_THROUGH.format(route=route))

        profiles_parser = subparsers.add_parser(COMMAND_PROFILES, help=MSG_PROFILES_HELP)
        profiles_parser.set_defaults(handler=_facade.list_profiles_command, repo_launchers=repo_launchers)

        return parser

    @staticmethod
    def split_application_args(argv: Sequence[str]) -> Tuple[List[str], List[str]]:
        values = list(argv)
        if COMMAND_SEPARATOR not in values:
            return values, []
        separator = values.index(COMMAND_SEPARATOR)
        return values[:separator], values[separator + 1 :]

    @staticmethod
    def main(argv: Sequence[str]) -> int:
        import launcher as _facade
        repo_launchers = _facade.discover_repo_launchers()
        if argv and argv[0] in repo_launchers:
            launcher = repo_launchers[argv[0]]
            return _facade.run_discovered_launcher(launcher, argv[1:], echo=not _facade.is_help_request(argv[1:]))

        parser_args, application_args = _facade.split_application_args(argv)
        parser = _facade.make_parser(repo_launchers)
        args = parser.parse_args(parser_args)
        args.application_args = application_args
        build_only = args.command == COMMAND_BUILD or (args.command == COMMAND_RUN and args.build_only)
        if build_only and application_args:
            parser.error(MSG_BUILD_APPLICATION_ARGS)
        if args.command == COMMAND_RUN and args.build_only and args.skip_build:
            parser.error(MSG_BUILD_ONLY_SKIP_BUILD)
        return args.handler(args)


    if __name__ == MAIN_ENTRY:
        raise SystemExit(main(sys.argv[1:]))


add_build_options = LauncherCli.add_build_options
add_common_options = LauncherCli.add_common_options
make_parser = LauncherCli.make_parser
split_application_args = LauncherCli.split_application_args
main = LauncherCli.main