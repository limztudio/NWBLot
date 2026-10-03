#!/usr/bin/env python3
"""BuildController: CMake build orchestration and executable resolution."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from launcher.models import LauncherModels, LaunchSettings
from launcher.constants import (
    CMAKE_BUILD_FLAG,
    CMAKE_CONFIG_FLAG,
    CMAKE_PARALLEL_FLAG,
    CMAKE_TARGET_FLAG,
    DEFAULT_DOMAIN,
    FILE_API_TARGET_EXECUTABLE,
    MSG_NOT_EXECUTABLE,
    MSG_NO_METADATA,
    MSG_NO_TARGETS,
    OPTION_WITH_PROFILE,
)


class BuildController:
    """CMake build orchestration and executable resolution."""

    @staticmethod
    def build_targets(args, settings: LaunchSettings, targets: Sequence[str], env: Dict[str, str]) -> None:
        import launcher as _facade
        if args.skip_build:
            return
        if not targets:
            raise ValueError(MSG_NO_TARGETS)

        command = list(settings.cmake) + [
            CMAKE_BUILD_FLAG,
            str(settings.build_dir),
            CMAKE_TARGET_FLAG,
            *targets,
            CMAKE_CONFIG_FLAG,
            settings.config,
        ]
        if args.jobs:
            command += [CMAKE_PARALLEL_FLAG, str(args.jobs)]
        _facade.run_checked(command, settings.root, env, args.dry_run)

    @staticmethod
    def build_target(args, settings: LaunchSettings, target: str, env: Dict[str, str]) -> None:
        import launcher as _facade
        _facade.build_targets(args, settings, (target,), env)

    @staticmethod
    def build_profile_targets(args, settings: LaunchSettings, env: Dict[str, str]) -> None:
        import launcher as _facade
        if not getattr(args, OPTION_WITH_PROFILE, False):
            return
        if args.profile_logserver_executable is not None:
            return
        _facade.build_target(args, settings, args.profile_logserver_target, env)

    @staticmethod
    def resolve_executable_path(
        settings: LaunchSettings,
        target: str,
        executable_override: Optional[Path],
        executable_base_name: Optional[str],
        dry_run: bool,
    ) -> Path:
        import launcher as _facade
        if executable_override is not None:
            return executable_override if executable_override.is_absolute() else settings.root / executable_override

        target_info = None if dry_run else _facade.load_cmake_target_info(settings.build_dir, target, settings.config)
        if target_info is not None:
            if target_info.target_type != FILE_API_TARGET_EXECUTABLE:
                raise SystemExit(MSG_NOT_EXECUTABLE.format(target=target))
            if target_info.artifacts:
                return target_info.artifacts[0]

        if not dry_run:
            print(MSG_NO_METADATA, flush=True)

        base_name = executable_base_name or _facade.target_default_executable_base_name(target)
        return _facade.output_root(settings.root, settings.platform_name, settings.arch, settings.domain) / settings.config / _facade.executable_name(
            base_name,
            settings.platform_name,
        )

    @staticmethod
    def resolve_working_directory(settings: LaunchSettings, override: Optional[Path], default_directory: Path) -> Path:
        import launcher as _facade
        if override is None:
            return default_directory
        return _facade.resolve_path(settings.root, override)

    @staticmethod
    def build_environment(_args) -> Dict[str, str]:
        return os.environ.copy()

    @staticmethod
    def normalize_application_args(args: Sequence[str]) -> List[str]:
        values = list(args)
        return values

    @staticmethod
    def build_command(args) -> int:
        import launcher as _facade
        env = _facade.build_environment(args)
        settings = _facade.resolve_launch_settings(args, DEFAULT_DOMAIN)
        _facade.maybe_configure(args, settings, {}, env)
        settings = _facade.refresh_launch_settings(settings, args.domain)
        _facade.build_targets(args, settings, args.targets, env)
        return 0


build_targets = BuildController.build_targets
build_target = BuildController.build_target
build_profile_targets = BuildController.build_profile_targets
resolve_executable_path = BuildController.resolve_executable_path
resolve_working_directory = BuildController.resolve_working_directory
build_environment = BuildController.build_environment
normalize_application_args = BuildController.normalize_application_args
build_command = BuildController.build_command