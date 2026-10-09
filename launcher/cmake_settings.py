#!/usr/bin/env python3
"""CmakeSettings: CMake configure/build settings, cache reads, and File API helpers."""

from __future__ import annotations

import json
import os
import shlex
import subprocess
from dataclasses import replace
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from launcher.models import CMakeTargetInfo, LaunchSettings
from launcher.constants import (
    ARCH_X64,
    ARG_JOIN_SEPARATOR,
    CACHE_ENTRY_SEPARATOR,
    CACHE_FILE_ENCODING,
    CACHE_FILE_ERRORS,
    CACHE_FILE_MODE,
    CACHE_KEY_SEPARATOR,
    CACHE_LINE_END,
    CMAKE_BINARY_FLAG,
    CMAKE_BOOL_FALSE_TOKENS,
    CMAKE_BOOL_TRUE_TOKENS,
    CMAKE_BUILD_ROOT_DIR,
    CMAKE_BUILD_SUBDIR,
    CMAKE_CACHE_FILE,
    CMAKE_COMMAND_ENV,
    CMAKE_DEFAULT_EXECUTABLE,
    CMAKE_DEFINE_EQUALS,
    CMAKE_DEFINE_PREFIX,
    CMAKE_LOCAL_BIN_POSIX,
    CMAKE_LOCAL_BIN_WINDOWS,
    CMAKE_OUTPUT_DOMAIN_KEY,
    CMAKE_PRESET_FLAG,
    CMAKE_SOURCE_FLAG,
    CMAKE_TOOL_VENV_DIR,
    CONFIGURE_ALWAYS,
    CONFIGURE_NEVER,
    DEFAULT_DOMAIN,
    DEFAULT_DOMAIN_FALLBACK,
    EMPTY_STRING,
    FILE_API_ARTIFACTS_KEY,
    FILE_API_CODEMODEL,
    FILE_API_CODEMODEL_VERSION,
    FILE_API_CONFIGURATIONS_KEY,
    FILE_API_DIR_API,
    FILE_API_DIR_CMAKE,
    FILE_API_DIR_QUERY,
    FILE_API_DIR_REPLY,
    FILE_API_DIR_V1,
    FILE_API_INDEX_GLOB,
    FILE_API_JSON_FILE_KEY,
    FILE_API_KIND_CODEMODEL,
    FILE_API_KIND_KEY,
    FILE_API_NAME_KEY,
    FILE_API_OBJECTS_KEY,
    FILE_API_PATH_KEY,
    FILE_API_TARGETS_KEY,
    FILE_API_TYPE_KEY,
    FILE_API_VERSION_KEY,
    FILE_API_VERSION_MAJOR,
    LOG_PREFIX,
    MSG_ARCH_PRESET_CONFLICT,
    MSG_CMAKE_DEFINE_EMPTY,
    MSG_CMAKE_DEFINE_USAGE,
    MSG_CONFIGURE_REQUIRED,
    OPTION_WITH_PROFILE,
    OS_WINDOWS,
    PLATFORM_WINDOWS,
    PRESET_ARCH_SEPARATOR,
    PRESET_PREFIX_FORMAT,
    PRESET_TOOLCHAIN,
    PROFILE_REQUIRED_DEFINES,
)


class CmakeSettings:
    """CMake configure/build settings, cache reads, and File API helpers."""

    @staticmethod
    def repo_root() -> Path:
        return Path(__file__).resolve().parents[1]

    @staticmethod
    def read_cmake_cache_value(build_dir: Path, key: str) -> Optional[str]:
        cache = build_dir / CMAKE_CACHE_FILE
        try:
            with cache.open(CACHE_FILE_MODE, encoding=CACHE_FILE_ENCODING, errors=CACHE_FILE_ERRORS) as cache_file:
                for line in cache_file:
                    prefix = key + CACHE_KEY_SEPARATOR
                    if line.startswith(prefix):
                        _, value = line.rstrip(CACHE_LINE_END).split(CACHE_ENTRY_SEPARATOR, 1)
                        return value
        except OSError:
            return None
        return None

    @staticmethod
    def infer_output_domain(build_dir: Path, platform_name: str, arch: str, configure_preset: Optional[str] = None) -> str:
        import launcher as _facade
        cached_domain = _facade.read_cmake_cache_value(build_dir, CMAKE_OUTPUT_DOMAIN_KEY)
        if cached_domain:
            return cached_domain

        name = configure_preset or build_dir.name
        if name == _facade.default_configure_preset_name(platform_name, DEFAULT_DOMAIN, arch):
            return DEFAULT_DOMAIN

        prefix = PRESET_PREFIX_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN)
        suffix = PRESET_ARCH_SEPARATOR + arch
        if name.startswith(prefix) and name.endswith(suffix):
            domain = name[len(prefix) : -len(suffix)]
            return domain or DEFAULT_DOMAIN_FALLBACK

        return name or DEFAULT_DOMAIN_FALLBACK

    @staticmethod
    def cmake_command(root: Path, override: Optional[Path], platform_name: Optional[str] = None) -> Tuple[str, ...]:
        import launcher as _facade
        if override is not None:
            return (str(override),)

        env_command = os.environ.get(CMAKE_COMMAND_ENV)
        if env_command:
            return (env_command,)

        local_bin_dir = CMAKE_LOCAL_BIN_WINDOWS if os.name == OS_WINDOWS else CMAKE_LOCAL_BIN_POSIX
        candidate_platform = platform_name or _facade.host_platform_name()
        candidate = root / CMAKE_BUILD_ROOT_DIR / CMAKE_TOOL_VENV_DIR / local_bin_dir / _facade.executable_name(CMAKE_DEFAULT_EXECUTABLE, candidate_platform)
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
        import launcher as _facade
        print(LOG_PREFIX + _facade.format_command(command), flush=True)
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
        import launcher as _facade
        for key, required_value in required_defines.items():
            cached_value = _facade.read_cmake_cache_value(build_dir, key)
            required_bool = _facade.normalize_cache_bool(required_value)
            cached_bool = _facade.normalize_cache_bool(cached_value)
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
        if not getattr(args, OPTION_WITH_PROFILE, False):
            return {}
        return dict(PROFILE_REQUIRED_DEFINES)

    @staticmethod
    def file_api_query_path(build_dir: Path) -> Path:
        return build_dir / FILE_API_DIR_CMAKE / FILE_API_DIR_API / FILE_API_DIR_V1 / FILE_API_DIR_QUERY / FILE_API_CODEMODEL

    @staticmethod
    def file_api_reply_dir(build_dir: Path) -> Path:
        return build_dir / FILE_API_DIR_CMAKE / FILE_API_DIR_API / FILE_API_DIR_V1 / FILE_API_DIR_REPLY

    @staticmethod
    def ensure_file_api_query(build_dir: Path) -> None:
        import launcher as _facade
        query = _facade.file_api_query_path(build_dir)
        query.parent.mkdir(parents=True, exist_ok=True)
        query.touch()

    @staticmethod
    def latest_file_api_index(build_dir: Path) -> Optional[Path]:
        import launcher as _facade
        reply_dir = _facade.file_api_reply_dir(build_dir)
        try:
            indexes = list(reply_dir.glob(FILE_API_INDEX_GLOB))
        except OSError:
            return None
        if not indexes:
            return None
        return max(indexes, key=lambda path: (path.stat().st_mtime, path.name))

    @staticmethod
    def file_api_has_reply(build_dir: Path) -> bool:
        import launcher as _facade
        return _facade.latest_file_api_index(build_dir) is not None

    @staticmethod
    def read_json(path: Path):
        with path.open(CACHE_FILE_MODE, encoding=CACHE_FILE_ENCODING) as file:
            return json.load(file)

    @staticmethod
    def load_cmake_target_info(build_dir: Path, target_name: str, config: str) -> Optional[CMakeTargetInfo]:
        import launcher as _facade
        index_path = _facade.latest_file_api_index(build_dir)
        if index_path is None:
            return None

        index = _facade.read_json(index_path)
        codemodel_file = None
        for item in index.get(FILE_API_OBJECTS_KEY, []):
            if item.get(FILE_API_KIND_KEY) != FILE_API_KIND_CODEMODEL:
                continue
            version = item.get(FILE_API_VERSION_KEY, {})
            if version.get(FILE_API_VERSION_MAJOR) == FILE_API_CODEMODEL_VERSION:
                codemodel_file = item.get(FILE_API_JSON_FILE_KEY)
                break
        if not codemodel_file:
            return None

        reply_dir = index_path.parent
        codemodel = _facade.read_json(reply_dir / codemodel_file)
        configurations = codemodel.get(FILE_API_CONFIGURATIONS_KEY, [])
        configuration = next((entry for entry in configurations if entry.get(FILE_API_NAME_KEY) == config), None)
        if configuration is None:
            return None

        for target_ref in configuration.get(FILE_API_TARGETS_KEY, []):
            if target_ref.get(FILE_API_NAME_KEY) != target_name:
                continue

            target = _facade.read_json(reply_dir / target_ref[FILE_API_JSON_FILE_KEY])
            artifacts = []
            for artifact in target.get(FILE_API_ARTIFACTS_KEY, []):
                artifact_path = Path(artifact[FILE_API_PATH_KEY])
                if not artifact_path.is_absolute():
                    artifact_path = build_dir / artifact_path
                artifacts.append(artifact_path)

            return _facade.CMakeTargetInfo(
                name=target.get(FILE_API_NAME_KEY, target_name),
                target_type=target.get(FILE_API_TYPE_KEY, EMPTY_STRING),
                artifacts=tuple(artifacts),
            )

        return None

    @staticmethod
    def resolve_repo_root(value: Optional[Path]) -> Path:
        import launcher as _facade
        if value is not None:
            return value.resolve()
        return _facade.repo_root()

    @staticmethod
    def resolve_launch_settings(args, default_domain: str) -> LaunchSettings:
        import launcher as _facade
        root = _facade.resolve_repo_root(args.repo_root)
        platform_name = args.platform
        requested_build_dir = _facade.resolve_path(root, args.build_dir) if args.build_dir is not None else None
        preset_arch = _facade.configure_preset_architecture(args.configure_preset) if args.configure_preset else None
        if args.arch and preset_arch and args.arch != preset_arch:
            raise SystemExit(
                MSG_ARCH_PRESET_CONFLICT.format(args=args, preset_arch=preset_arch)
            )
        arch = args.arch or preset_arch or (_facade.host_arch_name() if platform_name == PLATFORM_WINDOWS else ARCH_X64)
        if args.configure_preset:
            configure_preset = args.configure_preset
            build_dir = requested_build_dir or root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / configure_preset
        else:
            requested_domain = args.domain or default_domain
            configure_preset = _facade.default_configure_preset_name(platform_name, requested_domain, arch)
            build_dir = requested_build_dir or _facade.default_build_dir(root, platform_name, requested_domain, arch)
        domain = args.domain or _facade.infer_output_domain(build_dir, platform_name, arch, configure_preset)
        return _facade.LaunchSettings(
            root=root,
            platform_name=platform_name,
            arch=arch,
            domain=domain,
            config=args.config,
            configure_preset=configure_preset,
            build_dir=build_dir,
            cmake=_facade.cmake_command(root, args.cmake, platform_name),
        )

    @staticmethod
    def refresh_launch_settings(settings: LaunchSettings, explicit_domain: Optional[str]) -> LaunchSettings:
        import launcher as _facade
        if explicit_domain:
            return settings
        domain = _facade.infer_output_domain(settings.build_dir, settings.platform_name, settings.arch, settings.configure_preset)
        if domain == settings.domain:
            return settings
        return replace(settings, domain=domain)

    @staticmethod
    def configure_command(settings: LaunchSettings, build_dir_was_configured: bool, extra_defines: Dict[str, str]) -> List[str]:
        import launcher as _facade
        preset_build_dir = settings.root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / settings.configure_preset
        if settings.build_dir == preset_build_dir:
            return list(settings.cmake) + [CMAKE_PRESET_FLAG, settings.configure_preset] + _facade.cmake_define_args(extra_defines)
        if build_dir_was_configured:
            return list(settings.cmake) + [CMAKE_SOURCE_FLAG, str(settings.root), CMAKE_BINARY_FLAG, str(settings.build_dir)] + _facade.cmake_define_args(extra_defines)
        return list(settings.cmake) + [CMAKE_PRESET_FLAG, settings.configure_preset, CMAKE_BINARY_FLAG, str(settings.build_dir)] + _facade.cmake_define_args(extra_defines)

    @staticmethod
    def maybe_configure(args, settings: LaunchSettings, required_defines: Dict[str, str], env: Dict[str, str]) -> None:
        import launcher as _facade
        build_dir_was_configured = (settings.build_dir / CMAKE_CACHE_FILE).exists()
        if not args.dry_run:
            _facade.ensure_file_api_query(settings.build_dir)

        extra_defines = _facade.parse_define_entries(args.defines)
        extra_defines.update(required_defines)

        needs_configure = (
            args.configure == CONFIGURE_ALWAYS
            or bool(args.defines)
            or not build_dir_was_configured
            or not _facade.file_api_has_reply(settings.build_dir)
            or not _facade.cache_matches_required_defines(settings.build_dir, required_defines)
        )
        if not needs_configure:
            return

        if args.configure == CONFIGURE_NEVER:
            raise SystemExit(MSG_CONFIGURE_REQUIRED.format(settings=settings))

        command = _facade.configure_command(settings, build_dir_was_configured, extra_defines)
        _facade.run_checked(command, settings.root, env, args.dry_run)

    @staticmethod
    def resolve_path(root: Path, path: Path) -> Path:
        return path if path.is_absolute() else root / path


repo_root = CmakeSettings.repo_root
read_cmake_cache_value = CmakeSettings.read_cmake_cache_value
infer_output_domain = CmakeSettings.infer_output_domain
cmake_command = CmakeSettings.cmake_command
format_command = CmakeSettings.format_command
run_checked = CmakeSettings.run_checked
parse_define_entries = CmakeSettings.parse_define_entries
cmake_define_args = CmakeSettings.cmake_define_args
normalize_cache_bool = CmakeSettings.normalize_cache_bool
cache_matches_required_defines = CmakeSettings.cache_matches_required_defines
merged_required_defines = CmakeSettings.merged_required_defines
profile_required_defines = CmakeSettings.profile_required_defines
file_api_query_path = CmakeSettings.file_api_query_path
file_api_reply_dir = CmakeSettings.file_api_reply_dir
ensure_file_api_query = CmakeSettings.ensure_file_api_query
latest_file_api_index = CmakeSettings.latest_file_api_index
file_api_has_reply = CmakeSettings.file_api_has_reply
read_json = CmakeSettings.read_json
load_cmake_target_info = CmakeSettings.load_cmake_target_info
resolve_repo_root = CmakeSettings.resolve_repo_root
resolve_launch_settings = CmakeSettings.resolve_launch_settings
refresh_launch_settings = CmakeSettings.refresh_launch_settings
configure_command = CmakeSettings.configure_command
maybe_configure = CmakeSettings.maybe_configure
resolve_path = CmakeSettings.resolve_path