#!/usr/bin/env python3
"""BuildController: CMake build orchestration and executable resolution."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Dict, Optional, Sequence
from launcher.models import LaunchSettings
from launcher.cmake_settings import CmakeSettings
from launcher.cmake_file_api import CmakeFileApi
from launcher.host import HostProbe
from launcher.constants import (CMAKE_BUILD_FLAG, CMAKE_CONFIG_FLAG, CMAKE_PARALLEL_FLAG, CMAKE_TARGET_FLAG, DEFAULT_DOMAIN, FILE_API_TARGET_EXECUTABLE, MSG_NOT_EXECUTABLE, MSG_EXECUTABLE_ARTIFACT_REQUIRED, MSG_EXECUTABLE_METADATA_REQUIRED, MSG_NO_TARGETS,)


class BuildController:
    """CMake build orchestration and executable resolution."""

    @staticmethod
    def build_targets(args, settings: LaunchSettings, targets: Sequence[str], env: Dict[str, str]) -> None:
        if args.skip_build:
            return
        if not targets:
            raise ValueError(MSG_NO_TARGETS)

        command = list(settings.cmake) + [CMAKE_BUILD_FLAG, str(settings.build_dir), CMAKE_TARGET_FLAG, *targets, CMAKE_CONFIG_FLAG, settings.config,]
        if args.jobs:
            command += [CMAKE_PARALLEL_FLAG, str(args.jobs)]
        CmakeSettings.run_checked(command, settings.root, env, args.dry_run)

    @staticmethod
    def build_target(args, settings: LaunchSettings, target: str, env: Dict[str, str]) -> None:
        BuildController.build_targets(args, settings, (target,), env)

    @staticmethod
    def build_profile_targets(args, settings: LaunchSettings, env: Dict[str, str]) -> None:
        if not args.with_profile:
            return
        if args.profile_logserver_executable is not None:
            return
        BuildController.build_target(args, settings, args.profile_logserver_target, env)

    @staticmethod
    def resolve_executable_path(settings: LaunchSettings, target: str, executable_override: Optional[Path], executable_base_name: Optional[str], dry_run: bool,) -> Path:
        if executable_override is not None:
            return executable_override if executable_override.is_absolute() else settings.root / executable_override

        if not dry_run:
            target_info = CmakeFileApi.load_cmake_target_info(settings.build_dir, target, settings.config)
            if target_info is None:
                raise SystemExit(MSG_EXECUTABLE_METADATA_REQUIRED.format(target=target, config=settings.config))
            if target_info.target_type != FILE_API_TARGET_EXECUTABLE:
                raise SystemExit(MSG_NOT_EXECUTABLE.format(target=target))
            if not target_info.artifacts:
                raise SystemExit(MSG_EXECUTABLE_ARTIFACT_REQUIRED.format(target=target, config=settings.config))
            return target_info.artifacts[0]

        base_name = executable_base_name or HostProbe.target_default_executable_base_name(target)
        return HostProbe.output_root(settings.root, settings.platform_name, settings.arch, settings.domain) / settings.config / HostProbe.executable_name(base_name, settings.platform_name,)

    @staticmethod
    def resolve_working_directory(settings: LaunchSettings, override: Optional[Path], default_directory: Path) -> Path:
        if override is None:
            return default_directory
        return CmakeSettings.resolve_path(settings.root, override)

    @staticmethod
    def build_environment(_args) -> Dict[str, str]:
        return os.environ.copy()

    @staticmethod
    def build_command(args) -> int:
        env = BuildController.build_environment(args)
        settings = CmakeSettings.resolve_launch_settings(args, DEFAULT_DOMAIN)
        CmakeSettings.maybe_configure(args, settings, {}, env)
        settings = CmakeSettings.refresh_launch_settings(settings, args.domain)
        BuildController.build_targets(args, settings, args.targets, env)
        return 0
