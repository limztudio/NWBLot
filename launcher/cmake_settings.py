#!/usr/bin/env python3
"""CmakeSettings: CMake configuration, cache settings, and launch paths."""

from __future__ import annotations

import os
import shlex
import subprocess
from dataclasses import replace
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from launcher.models import LaunchSettings
from launcher.host import HostProbe
from launcher.cmake_file_api import CmakeFileApi
from launcher.constants import (ARCH_X64, ARG_JOIN_SEPARATOR, CACHE_ENTRY_SEPARATOR, CACHE_FILE_ENCODING, CACHE_FILE_ERRORS, CACHE_FILE_MODE,
    CACHE_KEY_SEPARATOR, CACHE_LINE_END, CMAKE_BINARY_FLAG, CMAKE_BOOL_FALSE_TOKENS, CMAKE_BOOL_TRUE_TOKENS, CMAKE_BUILD_ROOT_DIR, CMAKE_BUILD_SUBDIR,
    CMAKE_CACHE_FILE, CMAKE_COMMAND_ENV, CMAKE_DEFAULT_EXECUTABLE, CMAKE_DEFINE_EQUALS, CMAKE_DEFINE_PREFIX, CMAKE_LOCAL_BIN_POSIX,
    CMAKE_LOCAL_BIN_WINDOWS, CMAKE_OUTPUT_DOMAIN_KEY, CMAKE_PRESET_FLAG, CMAKE_SOURCE_FLAG, CMAKE_TOOL_VENV_DIR, CONFIGURE_ALWAYS, CONFIGURE_NEVER,
    DEFAULT_DOMAIN, DEFAULT_DOMAIN_FALLBACK, LOG_PREFIX, MSG_ARCH_PRESET_CONFLICT, MSG_CMAKE_DEFINE_EMPTY, MSG_CMAKE_DEFINE_USAGE,
    MSG_CONFIGURE_REQUIRED, OS_WINDOWS, PLATFORM_WINDOWS, PRESET_ARCH_SEPARATOR, PRESET_PREFIX_FORMAT, PRESET_TOOLCHAIN, PROFILE_REQUIRED_DEFINES,)


class CmakeSettings:
    """CMake configuration, cache settings, and launch paths."""

    @staticmethod
    def repo_root() -> Path:
        return Path(__file__).resolve().parents[1]

    @staticmethod
    def read_cmake_cache_value(build_dir: Path, key: str) -> Optional[str]:
        cache = build_dir / CMAKE_CACHE_FILE
        prefix = key + CACHE_KEY_SEPARATOR
        try:
            with cache.open(CACHE_FILE_MODE, encoding=CACHE_FILE_ENCODING, errors=CACHE_FILE_ERRORS) as cache_file:
                for line in cache_file:
                    if line.startswith(prefix):
                        _, value = line.rstrip(CACHE_LINE_END).split(CACHE_ENTRY_SEPARATOR, 1)
                        return value
        except FileNotFoundError:
            return None
        return None

    @staticmethod
    def infer_output_domain(build_dir: Path, platform_name: str, arch: str, configure_preset: Optional[str] = None) -> str:
        cached_domain = CmakeSettings.read_cmake_cache_value(build_dir, CMAKE_OUTPUT_DOMAIN_KEY)
        if cached_domain:
            return cached_domain

        name = configure_preset or build_dir.name
        if name == HostProbe.default_configure_preset_name(platform_name, DEFAULT_DOMAIN, arch):
            return DEFAULT_DOMAIN

        prefix = PRESET_PREFIX_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN)
        suffix = PRESET_ARCH_SEPARATOR + arch
        if name.startswith(prefix) and name.endswith(suffix):
            domain = name[len(prefix) : -len(suffix)]
            return domain or DEFAULT_DOMAIN_FALLBACK

        return name or DEFAULT_DOMAIN_FALLBACK

    @staticmethod
    def cmake_command(root: Path, override: Optional[Path], platform_name: Optional[str] = None) -> Tuple[str, ...]:
        if override is not None:
            return (str(override),)

        env_command = os.environ.get(CMAKE_COMMAND_ENV)
        if env_command:
            return (env_command,)

        local_bin_dir = CMAKE_LOCAL_BIN_WINDOWS if os.name == OS_WINDOWS else CMAKE_LOCAL_BIN_POSIX
        candidate_platform = platform_name or HostProbe.host_platform_name()
        candidate = root / CMAKE_BUILD_ROOT_DIR / CMAKE_TOOL_VENV_DIR / local_bin_dir / HostProbe.executable_name(CMAKE_DEFAULT_EXECUTABLE, candidate_platform)
        if candidate.exists():
            return (str(candidate),)

        return (CMAKE_DEFAULT_EXECUTABLE,)

    @staticmethod
    def format_command(command: Sequence[object]) -> str:
        parts = [str(part) for part in command]
        if os.name == OS_WINDOWS:
            return subprocess.list2cmdline(parts)
        return ARG_JOIN_SEPARATOR.join(shlex.quote(part) for part in parts)

    @staticmethod
    def run_checked(command: Sequence[object], cwd: Path, env: Dict[str, str], dry_run: bool = False) -> None:
        print(LOG_PREFIX + CmakeSettings.format_command(command), flush=True)
        if dry_run:
            return

        completed = subprocess.run([str(part) for part in command], cwd=cwd, env=env)
        if completed.returncode != 0:
            raise SystemExit(completed.returncode)

    @staticmethod
    def parse_define_entries(entries: Iterable[str]) -> Dict[str, str]:
        defines: Dict[str, str] = {}
        for entry in entries:
            if CMAKE_DEFINE_EQUALS not in entry:
                raise SystemExit(MSG_CMAKE_DEFINE_USAGE.format(entry=entry))
            key, value = entry.split(CMAKE_DEFINE_EQUALS, 1)
            if not key:
                raise SystemExit(MSG_CMAKE_DEFINE_EMPTY.format(entry=entry))
            defines[key] = value
        return defines

    @staticmethod
    def cmake_define_args(defines: Dict[str, str]) -> List[str]:
        return [CMAKE_DEFINE_PREFIX + key + CMAKE_DEFINE_EQUALS + value for key, value in sorted(defines.items())]

    @staticmethod
    def normalize_cache_bool(value: Optional[str]) -> Optional[bool]:
        if value is None:
            return None
        upper_value = value.upper()
        if upper_value in CMAKE_BOOL_TRUE_TOKENS:
            return True
        if upper_value in CMAKE_BOOL_FALSE_TOKENS:
            return False
        return None

    @staticmethod
    def cache_matches_required_defines(build_dir: Path, required_defines: Dict[str, str]) -> bool:
        for key, required_value in required_defines.items():
            cached_value = CmakeSettings.read_cmake_cache_value(build_dir, key)
            required_bool = CmakeSettings.normalize_cache_bool(required_value)
            cached_bool = CmakeSettings.normalize_cache_bool(cached_value)
            if required_bool is not None or cached_bool is not None:
                if cached_bool != required_bool:
                    return False
            elif cached_value != required_value:
                return False
        return True

    @staticmethod
    def merged_required_defines(*define_sets: Dict[str, str]) -> Dict[str, str]:
        merged: Dict[str, str] = {}
        for define_set in define_sets:
            merged.update(define_set)
        return merged

    @staticmethod
    def profile_required_defines(args) -> Dict[str, str]:
        if not args.with_profile:
            return {}
        return dict(PROFILE_REQUIRED_DEFINES)

    @staticmethod
    def resolve_repo_root(value: Optional[Path]) -> Path:
        if value is not None:
            return value.resolve()
        return CmakeSettings.repo_root()

    @staticmethod
    def resolve_launch_settings(args, default_domain: str) -> LaunchSettings:
        root = CmakeSettings.resolve_repo_root(args.repo_root)
        platform_name = args.platform
        requested_build_dir = CmakeSettings.resolve_path(root, args.build_dir) if args.build_dir is not None else None
        preset_arch = HostProbe.configure_preset_architecture(args.configure_preset) if args.configure_preset else None
        if args.arch and preset_arch and args.arch != preset_arch:
            raise SystemExit(MSG_ARCH_PRESET_CONFLICT.format(args=args, preset_arch=preset_arch))
        arch = args.arch or preset_arch or (HostProbe.host_arch_name() if platform_name == PLATFORM_WINDOWS else ARCH_X64)
        if args.configure_preset:
            configure_preset = args.configure_preset
            build_dir = requested_build_dir or root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / configure_preset
        else:
            requested_domain = args.domain or default_domain
            configure_preset = HostProbe.default_configure_preset_name(platform_name, requested_domain, arch)
            build_dir = requested_build_dir or HostProbe.default_build_dir(root, platform_name, requested_domain, arch)
        domain = args.domain or CmakeSettings.infer_output_domain(build_dir, platform_name, arch, configure_preset)
        return LaunchSettings(root=root, platform_name=platform_name, arch=arch, domain=domain, config=args.config, configure_preset=configure_preset, build_dir=build_dir, cmake=CmakeSettings.cmake_command(root, args.cmake, platform_name),)

    @staticmethod
    def refresh_launch_settings(settings: LaunchSettings, explicit_domain: Optional[str]) -> LaunchSettings:
        if explicit_domain:
            return settings
        domain = CmakeSettings.infer_output_domain(settings.build_dir, settings.platform_name, settings.arch, settings.configure_preset)
        if domain == settings.domain:
            return settings
        return replace(settings, domain=domain)

    @staticmethod
    def configure_command(settings: LaunchSettings, build_dir_was_configured: bool, extra_defines: Dict[str, str]) -> List[str]:
        preset_build_dir = settings.root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / settings.configure_preset
        if settings.build_dir == preset_build_dir:
            return list(settings.cmake) + [CMAKE_PRESET_FLAG, settings.configure_preset] + CmakeSettings.cmake_define_args(extra_defines)
        if build_dir_was_configured:
            return list(settings.cmake) + [CMAKE_SOURCE_FLAG, str(settings.root), CMAKE_BINARY_FLAG, str(settings.build_dir)] + CmakeSettings.cmake_define_args(extra_defines)
        return list(settings.cmake) + [CMAKE_PRESET_FLAG, settings.configure_preset, CMAKE_BINARY_FLAG, str(settings.build_dir)] + CmakeSettings.cmake_define_args(extra_defines)

    @staticmethod
    def maybe_configure(args, settings: LaunchSettings, required_defines: Dict[str, str], env: Dict[str, str]) -> None:
        build_dir_was_configured = (settings.build_dir / CMAKE_CACHE_FILE).exists()
        if not args.dry_run:
            CmakeFileApi.ensure_file_api_query(settings.build_dir)

        extra_defines = CmakeSettings.parse_define_entries(args.defines)
        extra_defines.update(required_defines)

        needs_configure = (args.configure == CONFIGURE_ALWAYS or bool(args.defines) or not build_dir_was_configured or not CmakeFileApi.file_api_has_reply(settings.build_dir) or not CmakeSettings.cache_matches_required_defines(settings.build_dir, required_defines))
        if not needs_configure:
            return

        if args.configure == CONFIGURE_NEVER:
            raise SystemExit(MSG_CONFIGURE_REQUIRED.format(settings=settings))

        command = CmakeSettings.configure_command(settings, build_dir_was_configured, extra_defines)
        CmakeSettings.run_checked(command, settings.root, env, args.dry_run)

    @staticmethod
    def resolve_path(root: Path, path: Path) -> Path:
        return path if path.is_absolute() else root / path
