#!/usr/bin/env python3
"""DispatchRouter: Build/run target command dispatch."""

from __future__ import annotations

from typing import Sequence
from launcher.constants import (
    COMMAND_HELP_LONG,
    COMMAND_HELP_SHORT,
    COMMAND_SEPARATOR,
    DEFAULT_DOMAIN,
)


class DispatchRouter:
    """Build/run target command dispatch."""

    @staticmethod
    def run_target_command(args) -> int:
        import launcher as _facade
        env = _facade.build_environment(args)
        settings = _facade.resolve_launch_settings(args, DEFAULT_DOMAIN)
        _facade.maybe_configure(args, settings, _facade.profile_required_defines(args), env)
        settings = _facade.refresh_launch_settings(settings, args.domain)
        _facade.build_target(args, settings, args.target, env)
        _facade.build_profile_targets(args, settings, env)
        if args.build_only:
            return 0

        executable = _facade.resolve_executable_path(settings, args.target, args.executable, args.executable_name, args.dry_run)
        working_directory = _facade.resolve_working_directory(
            settings,
            args.working_directory,
            _facade.output_root(settings.root, settings.platform_name, settings.arch, settings.domain),
        )
        return _facade.launch_with_optional_profile(
            args,
            settings,
            executable,
            working_directory,
            env,
            args.application_args,
        )

    @staticmethod
    def is_help_request(args: Sequence[str]) -> bool:
        for arg in args:
            if arg == COMMAND_SEPARATOR:
                return False
            if arg == COMMAND_HELP_SHORT or arg == COMMAND_HELP_LONG:
                return True
        return False


run_target_command = DispatchRouter.run_target_command
is_help_request = DispatchRouter.is_help_request
