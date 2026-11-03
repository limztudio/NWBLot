#!/usr/bin/env python3
"""DispatchRouter: Build/run target command dispatch."""

from __future__ import annotations

from typing import Sequence
from launcher.build import BuildController
from launcher.cmake_settings import CmakeSettings
from launcher.host import HostProbe
from launcher.profile import ProfileSessionController
from launcher.constants import (COMMAND_HELP_LONG, COMMAND_HELP_SHORT, COMMAND_SEPARATOR, DEFAULT_DOMAIN,)


class DispatchRouter:
    """Build/run target command dispatch."""

    @staticmethod
    def run_target_command(args) -> int:
        env = BuildController.build_environment(args)
        settings = CmakeSettings.resolve_launch_settings(args, DEFAULT_DOMAIN)
        CmakeSettings.maybe_configure(args, settings, CmakeSettings.profile_required_defines(args), env)
        settings = CmakeSettings.refresh_launch_settings(settings, args.domain)
        BuildController.build_target(args, settings, args.target, env)
        BuildController.build_profile_targets(args, settings, env)
        if args.build_only:
            return 0

        executable = BuildController.resolve_executable_path(settings, args.target, args.executable, args.executable_name, args.dry_run)
        working_directory = BuildController.resolve_working_directory(settings, args.working_directory, HostProbe.output_root(settings.root, settings.platform_name, settings.arch, settings.domain),)
        return ProfileSessionController.launch_with_optional_profile(args, settings, executable, working_directory, env, args.application_args,)

    @staticmethod
    def is_help_request(args: Sequence[str]) -> bool:
        for arg in args:
            if arg == COMMAND_SEPARATOR:
                return False
            if arg == COMMAND_HELP_SHORT or arg == COMMAND_HELP_LONG:
                return True
        return False
