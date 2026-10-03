#!/usr/bin/env python3
import argparse
import ctypes
import json
import os
import platform
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

from launcher import repository_windows_process


ARCH_X64_LITERAL = "x64"
ARCH_ARM64_LITERAL = "arm64"
CONFIGURATIONS = ("dbg", "opt", "fin")
SUPPORTED_ARCHITECTURES = (ARCH_X64_LITERAL, ARCH_ARM64_LITERAL)
WINDOWS_IMAGE_FILE_MACHINE_AMD64 = 0x8664
WINDOWS_IMAGE_FILE_MACHINE_ARM64 = 0xAA64
WINDOWS_NATIVE_MACHINE_NAMES = {
    WINDOWS_IMAGE_FILE_MACHINE_AMD64: "AMD64",
    WINDOWS_IMAGE_FILE_MACHINE_ARM64: "ARM64",
}
DEFAULT_CONFIG = "dbg"
DEFAULT_DOMAIN = "full"
DEFAULT_BUILD_JOBS = "8"
LAUNCHER_SEARCH_ROOTS = (Path("CoolStuff"), Path("tests"), Path("utilities"), Path("pipeline"))
LAUNCHER_SCRIPT_NAME = "launch.py"
RESERVED_LAUNCH_COMMANDS = frozenset(("build", "profiles", "run"))
PROFILE_LOGSERVER_TARGET = "nwb_logserver"
LOGSERVER_LABEL = "logserver"
PROFILE_LOGSERVER_EXECUTABLE = LOGSERVER_LABEL
PROFILE_LOG_ADDRESS = "http://localhost"
PROFILE_LOGSERVER_TIMEOUT_SECONDS = 10.0
PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS = 5.0
APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS = 5.0
APPLICATION_FORCED_STOP_TIMEOUT_SECONDS = 5.0
PROFILE_LOG_HOST = "127.0.0.1"
PROFILE_LOG_PORT_AUTO = 0
PROFILE_LOG_PORT_MIN = 0
PROFILE_LOG_PORT_MAX = 65535
PROFILE_LOG_DRY_RUN_PORT = 7117
PROFILE_LOG_CONNECT_TIMEOUT_SECONDS = 0.25
PROFILE_LOG_READY_POLL_SECONDS = 0.05
OS_WINDOWS = "nt"
PRESET_NAME_FORMAT = "{platform_name}-{toolchain}-{arch}"
PRESET_NAME_DOMAIN_FORMAT = "{platform_name}-{toolchain}-{domain}-{arch}"
PRESET_PREFIX_FORMAT = "{platform_name}-{toolchain}-"
FILE_API_OBJECTS_KEY = "objects"
PROFILE_REQUIRED_DEFINES = {
    "NWB_BUILD_LOGSERVER": "ON",
}
# Shared literals for launcher body (no inline hardcodes below this block).
ARCH_X64 = ARCH_X64_LITERAL
ARCH_ARM64 = ARCH_ARM64_LITERAL
ARCH_AMD64_ALIAS = "amd64"
ARCH_X86_64_ALIAS = "x86_64"
ARCH_X86_64_DASH_ALIAS = "x86-64"
ARCH_AARCH64_ALIAS = "aarch64"
PLATFORM_WINDOWS = "windows"
PLATFORM_LINUX = "linux"
PLATFORM_DARWIN = "darwin"
PLATFORM_WINDOWS_SYSTEM = "Windows"
PLATFORM_LINUX_SYSTEM = "Linux"
PLATFORM_DARWIN_SYSTEM = "Darwin"
ENGINE_DOMAIN = "engine"
NWB_TARGET_PREFIX = "nwb_"
CMAKE_CACHE_FILE = "CMakeCache.txt"
CMAKE_OUTPUT_DOMAIN_KEY = "NWB_OUTPUT_DOMAIN"
CMAKE_COMMAND_ENV = "CMAKE_COMMAND"
CMAKE_DEFAULT_EXECUTABLE = "cmake"
CMAKE_TOOL_VENV_DIR = "tool-venv"
CMAKE_LOCAL_BIN_WINDOWS = "Scripts"
CMAKE_LOCAL_BIN_POSIX = "bin"
CMAKE_BUILD_ROOT_DIR = "__cmake"
CMAKE_BUILD_SUBDIR = "build"
EXEC_OUTPUT_ROOT_DIR = "__exec"
EXEC_WINDOWS_SUFFIX = ".exe"
FILE_API_DIR_CMAKE = ".cmake"
FILE_API_DIR_API = "api"
FILE_API_DIR_V1 = "v1"
FILE_API_DIR_QUERY = "query"
FILE_API_DIR_REPLY = "reply"
FILE_API_CODEMODEL = "codemodel-v2"
FILE_API_INDEX_GLOB = "index-*.json"
FILE_API_KIND_KEY = "kind"
FILE_API_KIND_CODEMODEL = "codemodel"
FILE_API_VERSION_KEY = "version"
FILE_API_VERSION_MAJOR = "major"
FILE_API_CODEMODEL_VERSION = 2
FILE_API_JSON_FILE_KEY = "jsonFile"
FILE_API_CONFIGURATIONS_KEY = "configurations"
FILE_API_NAME_KEY = "name"
FILE_API_TARGETS_KEY = "targets"
FILE_API_ARTIFACTS_KEY = "artifacts"
FILE_API_PATH_KEY = "path"
FILE_API_TYPE_KEY = "type"
FILE_API_TARGET_EXECUTABLE = "EXECUTABLE"
COMMAND_SEPARATOR = "--"
COMMAND_BUILD = "build"
COMMAND_RUN = "run"
COMMAND_PROFILES = "profiles"
COMMAND_HELP_SHORT = "-h"
COMMAND_HELP_LONG = "--help"
ROUTE_SEPARATOR = " -> "
LOG_PREFIX = "+ "
CWD_PREFIX = "  cwd: "
LAUNCH_COMMAND_PATTERN = r"[a-z0-9]+(?:-[a-z0-9]+)*"
LAUNCH_COMMAND_NAME_UNDERSCORE = "_"
LAUNCH_COMMAND_NAME_HYPHEN = "-"
LAUNCHER_GLOB_SUFFIX = f"*/{LAUNCHER_SCRIPT_NAME}"
KIND_CATEGORY = "category"
KIND_DIRECTORY = "directory"
CONFIGURE_ALWAYS = "always"
CONFIGURE_NEVER = "never"
CONFIGURE_AUTO = "auto"
DEFINE_ACTION = "append"
DEFINE_METAVAR = "KEY=VALUE"
STORE_TRUE = "store_true"
NARGS_ONE_OR_MORE = "+"
EMPTY_STRING = ""
ARG_JOIN_SEPARATOR = " "
DEFAULT_DOMAIN_FALLBACK = "default"
CACHE_FILE_ENCODING = "utf-8"
CACHE_FILE_ERRORS = "replace"
CACHE_FILE_MODE = "r"
CACHE_KEY_SEPARATOR = ":"
CACHE_ENTRY_SEPARATOR = "="
CACHE_LINE_END = "\n"
CMAKE_DEFINE_EQUALS = "="
CMAKE_DEFINE_PREFIX = "-D"
CMAKE_BOOL_TRUE_TOKENS = ("1", "ON", "TRUE", "YES")
CMAKE_BOOL_FALSE_TOKENS = ("0", "OFF", "FALSE", "NO")
WINDOWS_KERNEL32 = "kernel32"
WINDOWS_WOW64_PROC2 = "IsWow64Process2"
WINDOWS_ENV_ARCH6432 = "PROCESSOR_ARCHITEW6432"
WINDOWS_ENV_ARCH = "PROCESSOR_ARCHITECTURE"
PRESET_ARCH_SEPARATOR = "-"
PRESET_TOOLCHAIN = "clang"
OPTION_WITH_PROFILE = "with_profile"
OPTION_RUN_SECONDS = "run_seconds"
STOP_KIND_FORCED = "forced"
STOP_KIND_GRACEFUL = "graceful"
PKILL_COMMAND = "pkill"
PKILL_FOLLOW_FLAG = "-f"
GPUDBG_FLAG = "--gpudbg"
PROFILE_CLIENT_ADDRESS_FLAG = "-a"
PROFILE_CLIENT_PORT_FLAG = "-p"
CMAKE_PRESET_FLAG = "--preset"
CMAKE_SOURCE_FLAG = "-S"
CMAKE_BINARY_FLAG = "-B"
CMAKE_BUILD_FLAG = "--build"
CMAKE_TARGET_FLAG = "--target"
CMAKE_CONFIG_FLAG = "--config"
CMAKE_PARALLEL_FLAG = "--parallel"
LIST_ITEM_SEPARATOR = ", "
MSG_DUPLICATE_COMMAND = f"duplicate launch command '{{command}}': {{existing.script}} and {{launcher.script}}; "
MSG_DISAMBIGUATE = "rename one leaf directory to disambiguate"
MSG_INVALID_COMMAND = f"invalid launch command '{{command}}' in {{source}}; use lowercase letters, digits, and single hyphens"
MSG_COMMAND_CONFLICT = f"launch command '{{command}}' in {{source}} conflicts with a built-in launcher command"
MSG_MISSING_LAUNCHER_PREFIX = "missing "
MSG_UNSUPPORTED_ARCH = f"unsupported host architecture '{{machine}}'; NWBLot supports x64 and arm64"
MSG_ARCH_PRESET_CONFLICT = f"--arch {{args.arch}} conflicts with configure preset '{{args.configure_preset}}' ({{preset_arch}})"
MSG_CONFIGURE_REQUIRED = f"CMake configure is required for {{settings.build_dir}}, but --configure=never was requested"
MSG_NO_TARGETS = "at least one CMake target is required"
MSG_NOT_EXECUTABLE = f"CMake target is not executable: {{target}}"
MSG_NO_METADATA = "warning: CMake target metadata unavailable; using repository executable naming convention"
MSG_NO_PKILL = "warning: --kill-existing requested, but pkill is not available on this host"
MSG_MISSING_EXECUTABLE = f"missing executable: {{executable}}"
MSG_MISSING_WORKDIR = f"missing working directory: {{working_directory}}"
MSG_PORT_BUSY = f"profile log port {{port}} is not available: {{exc}}"
MSG_PORT_RANGE = f"--profile-log-port must be between {PROFILE_LOG_PORT_MIN} and {PROFILE_LOG_PORT_MAX}"
MSG_LOGSERVER_EXITED = f"logserver exited before port {{port}} became ready (exit {{process.returncode}})"
MSG_LOGSERVER_TIMEOUT_SUFFIX = ": {last_error}"
MSG_LOGSERVER_NO_ACCEPT = f"logserver did not accept TCP connections on port {{port}} within {{timeout_seconds:.1f}}s{{suffix}}"
MSG_POSITIVE_TIMEOUT = "--profile-logserver-timeout must be positive"
MSG_LAUNCHED_LOGSERVER = f"launched {{logserver_executable.name}} pid={{process.pid}} port={{log_port}}"
MSG_LAUNCHED_APP = f"launched {{executable.name}} pid={{process.pid}}"
MSG_STOPPED_APP = f"stopped {{executable.name}} pid={{result.pid}} ({{stop_kind}}, exit {{result.exit_code}})"
MSG_STOPPING = f"stopping {{label}} pid={{process.pid}}"
MSG_KILLING = f"killing {{label}} pid={{process.pid}}"
MSG_RUN_GRACEFUL = f"run-seconds {{run_seconds}} elapsed; requested graceful app shutdown"
MSG_RUN_FORCED = f"forced {{executable.name}} shutdown pid={{process.pid}} (exit {{run_result.exit_code}})"
MSG_RUN_REQUEST = f"run-seconds {{run_seconds}} elapsed; requesting app shutdown"
MSG_NO_EXIT_STATUS = f"{{executable.name}} did not report an exit status after termination"
MSG_LEAVING_BOTH = "leaving app and logserver running; close them when done"
MSG_LEAVING_APP = "leaving app running; close the window when done"
MSG_UNKNOWN_LAUNCHER = f"unknown {{directory.name}} launcher '{{values[0]}}' (valid: {{valid}})"
MSG_BUILD_USAGE = "  build <targets...> [build options]"
MSG_RUN_USAGE = "  run <cmake-target> [launcher options] [-- application arguments]"
MSG_CMAKE_DEFINE_USAGE = "CMake define must be KEY=VALUE: {entry}"
MSG_CMAKE_DEFINE_EMPTY = "CMake define key must not be empty: {entry}"
ARG_REPO_ROOT = "--repo-root"
ARG_PLATFORM = "--platform"
ARG_ARCH = "--arch"
ARG_DOMAIN = "--domain"
ARG_CONFIGURE_PRESET = "--configure-preset"
ARG_BUILD_DIR = "--build-dir"
ARG_CMAKE = "--cmake"
ARG_JOBS = "--jobs"
ARG_CONFIGURE = "--configure"
ARG_DEFINE_SHORT = "-D"
ARG_DEFINE_LONG = "--define"
ARG_DEFINE_DEST = "defines"
ARG_SKIP_BUILD = "--skip-build"
ARG_BUILD_ONLY = "--build-only"
ARG_DRY_RUN = "--dry-run"
ARG_WORKING_DIRECTORY = "--working-directory"
ARG_EXECUTABLE = "--executable"
ARG_EXECUTABLE_NAME = "--executable-name"
ARG_GPUDbg = "--gpudbg"
ARG_KILL_EXISTING = "--kill-existing"
ARG_DETACH = "--detach"
ARG_RUN_SECONDS = "--run-seconds"
ARG_WITH_PROFILE = "--with-profile"
ARG_PROFILE_LOG_ADDRESS = "--profile-log-address"
ARG_PROFILE_LOG_PORT = "--profile-log-port"
ARG_PROFILE_LOGSERVER_TARGET = "--profile-logserver-target"
ARG_PROFILE_LOGSERVER_NAME = "--profile-logserver-name"
ARG_PROFILE_LOGSERVER_EXECUTABLE = "--profile-logserver-executable"
ARG_PROFILE_LOGSERVER_TIMEOUT = "--profile-logserver-timeout"
ARG_PROFILE_LOGSERVER_ARG = "--profile-logserver-arg"
ARG_PROFILE_LOGSERVER_ARGS_DEST = "profile_logserver_args"
ARG_TARGET = "target"
ARG_TARGETS = "targets"
ARG_COMMAND = "command"
MSG_LIST_COMMANDS = "runnable commands:"
MSG_ROUTE_ITEM = "  {launcher.command}  ({route})"
MSG_DIR_COMMANDS = "runnable {directory.name} commands:"
MSG_DIR_ITEM = "  {launcher.command}  ({launcher.script})"
MSG_FORWARD_THROUGH = "Forward through {route}."
MSG_BUILD_TARGET_HELP = "Configure and build targets without launching an application."
MSG_RUN_TARGET_HELP = "Build and launch a CMake executable target."
MSG_PROFILES_HELP = "List generic and discovered launch commands."
MSG_LAUNCHER_DESC = "Configure, build, and launch NWB targets."
MSG_TARGET_HELP = "CMake executable target, such as testbed or nwb_asset_builder."
MSG_BUILD_TARGETS_HELP = "One or more executable, library, or aggregate targets."
MSG_BUILD_APPLICATION_ARGS = "build-only commands do not accept application arguments after --"
MSG_BUILD_ONLY_SKIP_BUILD = "--build-only cannot be combined with --skip-build"
CONFIGURE_CHOICES = (CONFIGURE_AUTO, CONFIGURE_ALWAYS, CONFIGURE_NEVER)
MAIN_ENTRY = "__main__"
HELP_REPO_ROOT = "Repository root. Defaults to the root launcher directory."
HELP_PLATFORM = "Output platform directory, such as windows/linux/darwin."
HELP_ARCH = "Target architecture. Defaults to the native architecture on Windows and x64 elsewhere."
HELP_DOMAIN = "Output domain directory. Defaults to full or the CMake cache."
HELP_CONFIGURE_PRESET = "CMake configure preset. Defaults from platform/domain/arch."
HELP_BUILD_DIR = "CMake build directory."
HELP_CMAKE = "CMake executable. Defaults to CMAKE_COMMAND, repo-local CMake, or cmake on PATH."
HELP_JOBS = "Parallel build jobs passed to cmake --build."
HELP_CONFIGURE = "Run CMake configure when needed, always, or never."
HELP_SKIP_BUILD = "Do not build before launching."
HELP_BUILD_ONLY = "Configure and build without launching the application or profiling server."
HELP_DRY_RUN = "Print configure/build/launch commands without executing them."
HELP_BUILD_DRY_RUN = "Print configure/build commands without executing them."
HELP_WORKING_DIRECTORY = "Override launch working directory."
HELP_EXECUTABLE = "Override executable path."
HELP_EXECUTABLE_NAME = "Override executable base name when CMake metadata is unavailable."
HELP_GPUDbg = "Append --gpudbg to the launched application."
HELP_KILL_EXISTING = "Stop running copies of the selected executable before launch; Windows matches the exact executable image path."
HELP_DETACH = "Return after launch instead of waiting for the app."
HELP_RUN_SECONDS = "After N seconds, gracefully close the launched app with an exact-process forced fallback after a bounded wait."
HELP_WITH_PROFILE = "Start nwb_logserver and connect the launched app to it."
HELP_PROFILE_LOG_ADDRESS = "Log address passed to the launched app when --with-profile is enabled."
HELP_PROFILE_LOG_PORT = "Logserver port for --with-profile. Defaults to an available localhost port."
HELP_PROFILE_LOGSERVER_TARGET = "CMake target used for the profiling logserver."
HELP_PROFILE_LOGSERVER_NAME = "Executable base name for the profiling logserver when CMake metadata is unavailable."
HELP_PROFILE_LOGSERVER_EXECUTABLE = "Override logserver executable path for --with-profile."
HELP_PROFILE_LOGSERVER_TIMEOUT = "Seconds to wait for the profiling logserver to accept connections."
HELP_PROFILE_LOGSERVER_ARG = "Extra argument passed to the profiling logserver; repeat as needed."


@dataclass(frozen=True)
class LaunchSettings:
    root: Path
    platform_name: str
    arch: str
    domain: str
    config: str
    configure_preset: str
    build_dir: Path
    cmake: Tuple[str, ...]


@dataclass(frozen=True)
class CMakeTargetInfo:
    name: str
    target_type: str
    artifacts: Tuple[Path, ...]


@dataclass(frozen=True)
class RepoLauncher:
    command: str
    script: Path
    route: Tuple[Path, ...] = ()


@dataclass(frozen=True)
class ProfileSession:
    log_port: int
    logserver_executable: Path
    process: Optional[subprocess.Popen]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def launch_command_from_directory(script: Path) -> str:
    return script.parent.name.lower().replace(LAUNCH_COMMAND_NAME_UNDERSCORE, LAUNCH_COMMAND_NAME_HYPHEN)


def validate_launch_command(command: str, source: Path) -> str:
    if not re.fullmatch(LAUNCH_COMMAND_PATTERN, command):
        raise SystemExit(
            MSG_INVALID_COMMAND.format(command=command, source=source)
        )
    if command in RESERVED_LAUNCH_COMMANDS:
        raise SystemExit(MSG_COMMAND_CONFLICT.format(command=command, source=source))
    return command


def discover_directory_launchers(directory: Path, root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
    """Discover the launchers directly below one router directory."""
    root = (root or repo_root()).resolve()
    search_path = directory if directory.is_absolute() else root / directory
    search_path = search_path.resolve()

    if not search_path.is_dir():
        return {}

    launchers: Dict[str, RepoLauncher] = {}
    for script in sorted(search_path.glob(LAUNCHER_GLOB_SUFFIX), key=lambda path: path.as_posix()):

        command = validate_launch_command(launch_command_from_directory(script), script)
        launcher = RepoLauncher(command, script.relative_to(root))
        existing = launchers.get(command)
        if existing is not None:
            raise SystemExit(
                MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher)
                + MSG_DISAMBIGUATE
            )
        launchers[command] = launcher

    return dict(sorted(launchers.items()))


def launcher_route(search_path: Path, leaf_script: Path, root: Path) -> Tuple[Path, ...]:
    relative_parts = leaf_script.parent.relative_to(search_path).parts
    route: List[Path] = []
    for depth in range(len(relative_parts)):
        directory = search_path.joinpath(*relative_parts[:depth])
        script = directory / LAUNCHER_SCRIPT_NAME
        if not script.is_file():
            kind = KIND_CATEGORY if depth == 0 else KIND_DIRECTORY
            raise SystemExit(f"{MSG_MISSING_LAUNCHER_PREFIX}{kind} launcher: {script.relative_to(root)}")
        if depth > 0:
            validate_launch_command(launch_command_from_directory(script), script)
        route.append(script.relative_to(root))
    return tuple(route)


def discover_leaf_launchers(directory: Path, root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
    """Discover runnable leaves below a category and retain their router routes.

    A ``launch.py`` with descendant launchers is a router.  A leaf has no nested
    launchers, and every directory between it and the category must provide a
    router so nested groupings cannot be bypassed.
    """
    root = (root or repo_root()).resolve()
    search_path = directory if directory.is_absolute() else root / directory
    search_path = search_path.resolve()

    if not search_path.is_dir():
        return {}

    scripts = sorted(search_path.rglob(LAUNCHER_SCRIPT_NAME), key=lambda path: path.as_posix())
    launchers: Dict[str, RepoLauncher] = {}
    for script in scripts:
        if any(nested_script != script for nested_script in script.parent.rglob(LAUNCHER_SCRIPT_NAME)):
            continue

        command = validate_launch_command(launch_command_from_directory(script), script)
        launcher = RepoLauncher(command, script.relative_to(root), launcher_route(search_path, script, root))
        existing = launchers.get(command)
        if existing is not None:
            raise SystemExit(
                MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher)
                + MSG_DISAMBIGUATE
            )
        launchers[command] = launcher

    return dict(sorted(launchers.items()))


def discover_repo_launchers(root: Optional[Path] = None) -> Dict[str, RepoLauncher]:
    root = (root or repo_root()).resolve()
    launchers: Dict[str, RepoLauncher] = {}

    for search_root in LAUNCHER_SEARCH_ROOTS:
        for command, launcher in discover_leaf_launchers(search_root, root).items():
            existing = launchers.get(command)
            if existing is not None:
                raise SystemExit(
                    MSG_DUPLICATE_COMMAND.format(command=command, existing=existing, launcher=launcher)
                    + MSG_DISAMBIGUATE
                )
            launchers[command] = launcher

    return dict(sorted(launchers.items()))


def host_platform_name(system_name: Optional[str] = None) -> str:
    system = system_name or platform.system()
    if system == PLATFORM_WINDOWS_SYSTEM:
        return PLATFORM_WINDOWS
    if system == PLATFORM_LINUX_SYSTEM:
        return PLATFORM_LINUX
    if system == PLATFORM_DARWIN_SYSTEM:
        return PLATFORM_DARWIN
    if system:
        return system.lower()
    return sys.platform.lower()


def query_windows_native_machine_name() -> Optional[str]:
    kernel32 = ctypes.WinDLL(WINDOWS_KERNEL32, use_last_error=True)
    is_wow64_process2 = getattr(kernel32, WINDOWS_WOW64_PROC2, None)
    if is_wow64_process2 is None:
        return None

    get_current_process = kernel32.GetCurrentProcess
    get_current_process.argtypes = []
    get_current_process.restype = ctypes.c_void_p
    is_wow64_process2.argtypes = [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_ushort),
        ctypes.POINTER(ctypes.c_ushort),
    ]
    is_wow64_process2.restype = ctypes.c_int

    process_machine = ctypes.c_ushort(0)
    native_machine = ctypes.c_ushort(0)
    if not is_wow64_process2(get_current_process(), ctypes.byref(process_machine), ctypes.byref(native_machine)):
        return None
    return WINDOWS_NATIVE_MACHINE_NAMES.get(native_machine.value)


def windows_native_machine_name() -> Optional[str]:
    machine = query_windows_native_machine_name()
    if machine:
        return machine
    return os.environ.get(WINDOWS_ENV_ARCH6432) or os.environ.get(WINDOWS_ENV_ARCH)


def host_arch_name(machine_name: Optional[str] = None) -> str:
    if machine_name is None:
        machine_name = windows_native_machine_name() if platform.system() == PLATFORM_WINDOWS_SYSTEM else None
    machine = (machine_name or platform.machine()).lower()
    if machine in (ARCH_X64, ARCH_AMD64_ALIAS, ARCH_X86_64_ALIAS, ARCH_X86_64_DASH_ALIAS):
        return ARCH_X64
    if machine in (ARCH_ARM64, ARCH_AARCH64_ALIAS):
        return ARCH_ARM64
    raise SystemExit(MSG_UNSUPPORTED_ARCH.format(machine=machine))


def configure_preset_architecture(preset_name: str) -> Optional[str]:
    return next((arch for arch in SUPPORTED_ARCHITECTURES if preset_name.endswith(PRESET_ARCH_SEPARATOR + arch)), None)


def executable_name(base_name: str, platform_name: str) -> str:
    return base_name + EXEC_WINDOWS_SUFFIX if platform_name == PLATFORM_WINDOWS else base_name


def default_configure_preset_name(platform_name: str, domain: str, arch: str) -> str:
    if domain == DEFAULT_DOMAIN:
        return PRESET_NAME_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN, arch=arch)
    return PRESET_NAME_DOMAIN_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN, domain=domain, arch=arch)


def default_build_dir(root: Path, platform_name: str, domain: str, arch: str) -> Path:
    return root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / default_configure_preset_name(platform_name, domain, arch)


def output_root(root: Path, platform_name: str, arch: str, domain: str) -> Path:
    base = root / EXEC_OUTPUT_ROOT_DIR / platform_name / arch
    if domain == ENGINE_DOMAIN:
        return base
    return base / domain


def target_default_executable_base_name(target: str) -> str:
    if target.startswith(NWB_TARGET_PREFIX):
        return target[len(NWB_TARGET_PREFIX):]
    return target


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


def infer_output_domain(build_dir: Path, platform_name: str, arch: str, configure_preset: Optional[str] = None) -> str:
    cached_domain = read_cmake_cache_value(build_dir, CMAKE_OUTPUT_DOMAIN_KEY)
    if cached_domain:
        return cached_domain

    name = configure_preset or build_dir.name
    if name == default_configure_preset_name(platform_name, DEFAULT_DOMAIN, arch):
        return DEFAULT_DOMAIN

    prefix = PRESET_PREFIX_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN)
    suffix = PRESET_ARCH_SEPARATOR + arch
    if name.startswith(prefix) and name.endswith(suffix):
        domain = name[len(prefix) : -len(suffix)]
        return domain or DEFAULT_DOMAIN_FALLBACK

    return name or DEFAULT_DOMAIN_FALLBACK


def cmake_command(root: Path, override: Optional[Path], platform_name: Optional[str] = None) -> Tuple[str, ...]:
    if override is not None:
        return (str(override),)

    env_command = os.environ.get(CMAKE_COMMAND_ENV)
    if env_command:
        return (env_command,)

    local_bin_dir = CMAKE_LOCAL_BIN_WINDOWS if os.name == OS_WINDOWS else CMAKE_LOCAL_BIN_POSIX
    candidate_platform = platform_name or host_platform_name()
    candidate = root / CMAKE_BUILD_ROOT_DIR / CMAKE_TOOL_VENV_DIR / local_bin_dir / executable_name(CMAKE_DEFAULT_EXECUTABLE, candidate_platform)
    if candidate.exists():
        return (str(candidate),)

    return (CMAKE_DEFAULT_EXECUTABLE,)


def format_command(command: Sequence[object]) -> str:
    parts = [str(part) for part in command]
    if os.name == OS_WINDOWS:
        return subprocess.list2cmdline(parts)
    return ARG_JOIN_SEPARATOR.join(shlex.quote(part) for part in parts)


def run_checked(command: Sequence[object], cwd: Path, env: Dict[str, str], dry_run: bool = False) -> None:
    print(LOG_PREFIX + format_command(command), flush=True)
    if dry_run:
        return

    completed = subprocess.run([str(part) for part in command], cwd=cwd, env=env)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


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


def cmake_define_args(defines: Dict[str, str]) -> List[str]:
    return [CMAKE_DEFINE_PREFIX + key + CMAKE_DEFINE_EQUALS + value for key, value in sorted(defines.items())]


def normalize_cache_bool(value: Optional[str]) -> Optional[bool]:
    if value is None:
        return None
    upper_value = value.upper()
    if upper_value in CMAKE_BOOL_TRUE_TOKENS:
        return True
    if upper_value in CMAKE_BOOL_FALSE_TOKENS:
        return False
    return None


def cache_matches_required_defines(build_dir: Path, required_defines: Dict[str, str]) -> bool:
    for key, required_value in required_defines.items():
        cached_value = read_cmake_cache_value(build_dir, key)
        required_bool = normalize_cache_bool(required_value)
        cached_bool = normalize_cache_bool(cached_value)
        if required_bool is not None or cached_bool is not None:
            if cached_bool != required_bool:
                return False
        elif cached_value != required_value:
            return False
    return True


def merged_required_defines(*define_sets: Dict[str, str]) -> Dict[str, str]:
    merged: Dict[str, str] = {}
    for define_set in define_sets:
        merged.update(define_set)
    return merged


def profile_required_defines(args) -> Dict[str, str]:
    if not getattr(args, OPTION_WITH_PROFILE, False):
        return {}
    return dict(PROFILE_REQUIRED_DEFINES)


def file_api_query_path(build_dir: Path) -> Path:
    return build_dir / FILE_API_DIR_CMAKE / FILE_API_DIR_API / FILE_API_DIR_V1 / FILE_API_DIR_QUERY / FILE_API_CODEMODEL


def file_api_reply_dir(build_dir: Path) -> Path:
    return build_dir / FILE_API_DIR_CMAKE / FILE_API_DIR_API / FILE_API_DIR_V1 / FILE_API_DIR_REPLY


def ensure_file_api_query(build_dir: Path) -> None:
    query = file_api_query_path(build_dir)
    query.parent.mkdir(parents=True, exist_ok=True)
    query.touch()


def latest_file_api_index(build_dir: Path) -> Optional[Path]:
    reply_dir = file_api_reply_dir(build_dir)
    try:
        indexes = list(reply_dir.glob(FILE_API_INDEX_GLOB))
    except OSError:
        return None
    if not indexes:
        return None
    return max(indexes, key=lambda path: (path.stat().st_mtime, path.name))


def file_api_has_reply(build_dir: Path) -> bool:
    return latest_file_api_index(build_dir) is not None


def read_json(path: Path):
    with path.open(CACHE_FILE_MODE, encoding=CACHE_FILE_ENCODING) as file:
        return json.load(file)


def load_cmake_target_info(build_dir: Path, target_name: str, config: str) -> Optional[CMakeTargetInfo]:
    index_path = latest_file_api_index(build_dir)
    if index_path is None:
        return None

    index = read_json(index_path)
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
    codemodel = read_json(reply_dir / codemodel_file)
    configurations = codemodel.get(FILE_API_CONFIGURATIONS_KEY, [])
    configuration = next((entry for entry in configurations if entry.get(FILE_API_NAME_KEY) == config), None)
    if configuration is None and len(configurations) == 1:
        configuration = configurations[0]
    if configuration is None:
        return None

    for target_ref in configuration.get(FILE_API_TARGETS_KEY, []):
        if target_ref.get(FILE_API_NAME_KEY) != target_name:
            continue

        target = read_json(reply_dir / target_ref[FILE_API_JSON_FILE_KEY])
        artifacts = []
        for artifact in target.get(FILE_API_ARTIFACTS_KEY, []):
            artifact_path = Path(artifact[FILE_API_PATH_KEY])
            if not artifact_path.is_absolute():
                artifact_path = build_dir / artifact_path
            artifacts.append(artifact_path)

        return CMakeTargetInfo(
            name=target.get(FILE_API_NAME_KEY, target_name),
            target_type=target.get(FILE_API_TYPE_KEY, EMPTY_STRING),
            artifacts=tuple(artifacts),
        )

    return None


def resolve_repo_root(value: Optional[Path]) -> Path:
    if value is not None:
        return value.resolve()
    return repo_root()


def resolve_launch_settings(args, default_domain: str) -> LaunchSettings:
    root = resolve_repo_root(args.repo_root)
    platform_name = args.platform
    requested_build_dir = resolve_path(root, args.build_dir) if args.build_dir is not None else None
    preset_arch = configure_preset_architecture(args.configure_preset) if args.configure_preset else None
    if args.arch and preset_arch and args.arch != preset_arch:
        raise SystemExit(
            MSG_ARCH_PRESET_CONFLICT.format(args=args, preset_arch=preset_arch)
        )
    arch = args.arch or preset_arch or (host_arch_name() if platform_name == PLATFORM_WINDOWS else ARCH_X64)
    if args.configure_preset:
        configure_preset = args.configure_preset
        build_dir = requested_build_dir or root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / configure_preset
    else:
        requested_domain = args.domain or default_domain
        configure_preset = default_configure_preset_name(platform_name, requested_domain, arch)
        build_dir = requested_build_dir or default_build_dir(root, platform_name, requested_domain, arch)
    domain = args.domain or infer_output_domain(build_dir, platform_name, arch, configure_preset)
    return LaunchSettings(
        root=root,
        platform_name=platform_name,
        arch=arch,
        domain=domain,
        config=args.config,
        configure_preset=configure_preset,
        build_dir=build_dir,
        cmake=cmake_command(root, args.cmake, platform_name),
    )


def refresh_launch_settings(settings: LaunchSettings, explicit_domain: Optional[str]) -> LaunchSettings:
    if explicit_domain:
        return settings
    domain = infer_output_domain(settings.build_dir, settings.platform_name, settings.arch, settings.configure_preset)
    if domain == settings.domain:
        return settings
    return replace(settings, domain=domain)


def configure_command(settings: LaunchSettings, build_dir_was_configured: bool, extra_defines: Dict[str, str]) -> List[str]:
    preset_build_dir = settings.root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / settings.configure_preset
    if settings.build_dir == preset_build_dir:
        return list(settings.cmake) + [CMAKE_PRESET_FLAG, settings.configure_preset] + cmake_define_args(extra_defines)
    if build_dir_was_configured:
        return list(settings.cmake) + [CMAKE_SOURCE_FLAG, str(settings.root), CMAKE_BINARY_FLAG, str(settings.build_dir)] + cmake_define_args(extra_defines)
    return list(settings.cmake) + [CMAKE_PRESET_FLAG, settings.configure_preset, CMAKE_BINARY_FLAG, str(settings.build_dir)] + cmake_define_args(extra_defines)


def maybe_configure(args, settings: LaunchSettings, required_defines: Dict[str, str], env: Dict[str, str]) -> None:
    build_dir_was_configured = (settings.build_dir / CMAKE_CACHE_FILE).exists()
    if not args.dry_run:
        ensure_file_api_query(settings.build_dir)

    extra_defines = parse_define_entries(args.defines)
    extra_defines.update(required_defines)

    needs_configure = (
        args.configure == CONFIGURE_ALWAYS
        or bool(args.defines)
        or not build_dir_was_configured
        or not file_api_has_reply(settings.build_dir)
        or not cache_matches_required_defines(settings.build_dir, required_defines)
    )
    if not needs_configure:
        return

    if args.configure == CONFIGURE_NEVER:
        raise SystemExit(MSG_CONFIGURE_REQUIRED.format(settings=settings))

    command = configure_command(settings, build_dir_was_configured, extra_defines)
    run_checked(command, settings.root, env, args.dry_run)


def build_targets(args, settings: LaunchSettings, targets: Sequence[str], env: Dict[str, str]) -> None:
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
    run_checked(command, settings.root, env, args.dry_run)


def build_target(args, settings: LaunchSettings, target: str, env: Dict[str, str]) -> None:
    build_targets(args, settings, (target,), env)


def build_profile_targets(args, settings: LaunchSettings, env: Dict[str, str]) -> None:
    if not getattr(args, OPTION_WITH_PROFILE, False):
        return
    if args.profile_logserver_executable is not None:
        return
    build_target(args, settings, args.profile_logserver_target, env)


def resolve_executable_path(
    settings: LaunchSettings,
    target: str,
    executable_override: Optional[Path],
    executable_base_name: Optional[str],
    dry_run: bool,
) -> Path:
    if executable_override is not None:
        return executable_override if executable_override.is_absolute() else settings.root / executable_override

    target_info = None if dry_run else load_cmake_target_info(settings.build_dir, target, settings.config)
    if target_info is not None:
        if target_info.target_type != FILE_API_TARGET_EXECUTABLE:
            raise SystemExit(MSG_NOT_EXECUTABLE.format(target=target))
        if target_info.artifacts:
            return target_info.artifacts[0]

    if not dry_run:
        print(MSG_NO_METADATA, flush=True)

    base_name = executable_base_name or target_default_executable_base_name(target)
    return output_root(settings.root, settings.platform_name, settings.arch, settings.domain) / settings.config / executable_name(
        base_name,
        settings.platform_name,
    )


def resolve_working_directory(settings: LaunchSettings, override: Optional[Path], default_directory: Path) -> Path:
    if override is None:
        return default_directory
    return resolve_path(settings.root, override)


def resolve_path(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def build_environment(_args) -> Dict[str, str]:
    return os.environ.copy()


def normalize_application_args(args: Sequence[str]) -> List[str]:
    values = list(args)
    return values


def stop_existing_process(executable: Path, platform_name: str) -> None:
    if platform_name == PLATFORM_WINDOWS:
        results = repository_windows_process.stop_processes_by_image_path(
            executable,
            APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS,
            APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,
        )
        for result in results:
            stop_kind = STOP_KIND_FORCED if result.forced else STOP_KIND_GRACEFUL
            print(MSG_STOPPED_APP.format(executable=executable, result=result, stop_kind=stop_kind), flush=True)
        return

    if shutil.which(PKILL_COMMAND) is None:
        print(MSG_NO_PKILL, flush=True)
        return

    pattern = re.escape(str(executable.resolve()))
    subprocess.run([PKILL_COMMAND, PKILL_FOLLOW_FLAG, pattern], check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def validate_launch_paths(executable: Path, working_directory: Path, dry_run: bool) -> None:
    if dry_run:
        return
    if not executable.exists():
        raise SystemExit(MSG_MISSING_EXECUTABLE.format(executable=executable))
    if not working_directory.exists():
        raise SystemExit(MSG_MISSING_WORKDIR.format(working_directory=working_directory))


def terminate_process(process: Optional[subprocess.Popen], label: str, timeout_seconds: float = PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS) -> None:
    if process is None or process.poll() is not None:
        return

    print(MSG_STOPPING.format(label=label, process=process), flush=True)
    process.terminate()
    try:
        process.wait(timeout=timeout_seconds)
        return
    except subprocess.TimeoutExpired:
        print(MSG_KILLING.format(label=label, process=process), flush=True)
        process.kill()
        process.wait()


def wait_for_process_exit(process: subprocess.Popen, timeout_seconds: float) -> Optional[int]:
    deadline = time.monotonic() + timeout_seconds
    while True:
        exit_code = process.poll()
        if exit_code is not None:
            return exit_code

        remaining_seconds = deadline - time.monotonic()
        if remaining_seconds <= 0.0:
            return None
        time.sleep(min(PROFILE_LOG_READY_POLL_SECONDS, remaining_seconds))


def choose_free_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.bind((PROFILE_LOG_HOST, PROFILE_LOG_PORT_AUTO))
        return int(listener.getsockname()[1])


def ensure_tcp_port_available(port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        try:
            listener.bind((PROFILE_LOG_HOST, port))
        except OSError as exc:
            raise SystemExit(MSG_PORT_BUSY.format(port=port, exc=exc)) from exc


def resolve_profile_log_port(args) -> int:
    port = int(args.profile_log_port)
    if port < PROFILE_LOG_PORT_MIN or port > PROFILE_LOG_PORT_MAX:
        raise SystemExit(MSG_PORT_RANGE)
    if port != PROFILE_LOG_PORT_AUTO:
        if not args.dry_run:
            ensure_tcp_port_available(port)
        return port
    if args.dry_run:
        return PROFILE_LOG_DRY_RUN_PORT
    return choose_free_tcp_port()


def wait_for_tcp_port(port: int, timeout_seconds: float, process: Optional[subprocess.Popen]) -> None:
    deadline = time.monotonic() + timeout_seconds
    last_error: Optional[OSError] = None
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise SystemExit(MSG_LOGSERVER_EXITED.format(port=port, process=process))

        try:
            with socket.create_connection((PROFILE_LOG_HOST, port), timeout=PROFILE_LOG_CONNECT_TIMEOUT_SECONDS):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(PROFILE_LOG_READY_POLL_SECONDS)

    suffix = MSG_LOGSERVER_TIMEOUT_SUFFIX.format(last_error=last_error) if last_error is not None else EMPTY_STRING
    raise SystemExit(MSG_LOGSERVER_NO_ACCEPT.format(port=port, timeout_seconds=timeout_seconds, suffix=suffix))


def profile_client_args(args, profile_session: Optional[ProfileSession]) -> List[str]:
    if profile_session is None:
        return []
    return [PROFILE_CLIENT_ADDRESS_FLAG, args.profile_log_address, PROFILE_CLIENT_PORT_FLAG, str(profile_session.log_port)]


def start_profile_session(
    args,
    settings: LaunchSettings,
    working_directory: Path,
    env: Dict[str, str],
) -> Optional[ProfileSession]:
    if not getattr(args, OPTION_WITH_PROFILE, False):
        return None

    if args.profile_logserver_timeout <= 0.0:
        raise SystemExit(MSG_POSITIVE_TIMEOUT)

    log_port = resolve_profile_log_port(args)
    logserver_executable = resolve_executable_path(
        settings,
        args.profile_logserver_target,
        args.profile_logserver_executable,
        args.profile_logserver_name,
        args.dry_run,
    )
    validate_launch_paths(logserver_executable, working_directory, args.dry_run)

    command = [str(logserver_executable), PROFILE_CLIENT_PORT_FLAG, str(log_port)] + list(args.profile_logserver_args)
    print(LOG_PREFIX + format_command(command), flush=True)
    print(CWD_PREFIX + str(working_directory), flush=True)
    if args.dry_run:
        return ProfileSession(log_port, logserver_executable, None)

    process = subprocess.Popen(command, cwd=working_directory, env=env)
    print(MSG_LAUNCHED_LOGSERVER.format(logserver_executable=logserver_executable, process=process, log_port=log_port), flush=True)
    try:
        wait_for_tcp_port(log_port, args.profile_logserver_timeout, process)
    except BaseException:
        terminate_process(process, LOGSERVER_LABEL)
        raise

    return ProfileSession(log_port, logserver_executable, process)


def launch_process(
    args,
    executable: Path,
    working_directory: Path,
    env: Dict[str, str],
    application_args: Sequence[str],
    profile_session: Optional[ProfileSession] = None,
    paths_validated: bool = False,
) -> int:
    if not paths_validated:
        validate_launch_paths(executable, working_directory, args.dry_run)

    if args.kill_existing and not args.dry_run:
        stop_existing_process(executable, host_platform_name())

    launch = [str(executable)]
    if args.gpudbg:
        launch.append(GPUDBG_FLAG)
    launch += profile_client_args(args, profile_session)
    launch += list(application_args)

    print("+ " + format_command(launch), flush=True)
    print(CWD_PREFIX + str(working_directory), flush=True)
    if args.dry_run:
        return 0

    process: Optional[subprocess.Popen] = None
    try:
        process = subprocess.Popen(launch, cwd=working_directory, env=env)
        print(MSG_LAUNCHED_APP.format(executable=executable, process=process), flush=True)
        if args.detach:
            return 0

        run_seconds = getattr(args, OPTION_RUN_SECONDS, None)
        if run_seconds is not None and run_seconds > 0.0:
            if host_platform_name() == PLATFORM_WINDOWS:
                run_result = repository_windows_process.run_bounded_process(
                    process,
                    run_seconds,
                    APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS,
                    APPLICATION_FORCED_STOP_TIMEOUT_SECONDS,
                )
                if run_result.deadline_reached:
                    print(MSG_RUN_GRACEFUL.format(run_seconds=run_seconds), flush=True)
                if run_result.forced:
                    print(MSG_RUN_FORCED.format(executable=executable, process=process, run_result=run_result), flush=True)
                return run_result.exit_code

            exit_code = wait_for_process_exit(process, run_seconds)
            if exit_code is not None:
                return exit_code

            print(MSG_RUN_REQUEST.format(run_seconds=run_seconds), flush=True)
            terminate_process(process, executable.name)
            if process.returncode is None:
                raise SystemExit(MSG_NO_EXIT_STATUS.format(executable=executable))
            return process.returncode

        return process.wait()
    except KeyboardInterrupt:
        if profile_session is not None and profile_session.process is not None:
            print(MSG_LEAVING_BOTH, flush=True)
        else:
            print(MSG_LEAVING_APP, flush=True)
        return 0
    finally:
        if (
            profile_session is not None
            and profile_session.process is not None
            and not args.detach
            and (process is None or process.poll() is not None)
        ):
            terminate_process(profile_session.process, LOGSERVER_LABEL)


def launch_with_optional_profile(
    args,
    settings: LaunchSettings,
    executable: Path,
    working_directory: Path,
    env: Dict[str, str],
    application_args: Sequence[str],
) -> int:
    validate_launch_paths(executable, working_directory, args.dry_run)
    profile_session = start_profile_session(args, settings, working_directory, env)
    return launch_process(args, executable, working_directory, env, application_args, profile_session, paths_validated=True)


def build_command(args) -> int:
    env = build_environment(args)
    settings = resolve_launch_settings(args, DEFAULT_DOMAIN)
    maybe_configure(args, settings, {}, env)
    settings = refresh_launch_settings(settings, args.domain)
    build_targets(args, settings, args.targets, env)
    return 0


def run_target_command(args) -> int:
    env = build_environment(args)
    settings = resolve_launch_settings(args, DEFAULT_DOMAIN)
    maybe_configure(args, settings, profile_required_defines(args), env)
    settings = refresh_launch_settings(settings, args.domain)
    build_target(args, settings, args.target, env)
    build_profile_targets(args, settings, env)
    if args.build_only:
        return 0

    executable = resolve_executable_path(settings, args.target, args.executable, args.executable_name, args.dry_run)
    working_directory = resolve_working_directory(
        settings,
        args.working_directory,
        output_root(settings.root, settings.platform_name, settings.arch, settings.domain),
    )
    return launch_with_optional_profile(
        args,
        settings,
        executable,
        working_directory,
        env,
        normalize_application_args(args.application_args),
    )


def run_target_launcher(target: str, argv: Sequence[str]) -> int:
    return main([COMMAND_RUN, target] + list(argv))


def run_repo_script(script: Path, script_args: Sequence[str], echo: bool = True) -> int:
    root = repo_root()
    script_path = root / script
    command = [sys.executable, str(script_path)] + list(script_args)
    if echo:
        print(LOG_PREFIX + format_command(command), flush=True)
    return subprocess.run(command, cwd=root).returncode


def is_help_request(args: Sequence[str]) -> bool:
    for arg in args:
        if arg == COMMAND_SEPARATOR:
            return False
        if arg == COMMAND_HELP_SHORT or arg == COMMAND_HELP_LONG:
            return True
    return False


def run_discovered_launcher(repo_launcher: RepoLauncher, forwarded_args: Sequence[str], echo: bool = True) -> int:
    if repo_launcher.route:
        route_arguments = [launch_command_from_directory(script) for script in repo_launcher.route[1:]]
        return run_repo_script(
            repo_launcher.route[0],
            route_arguments + [repo_launcher.command] + list(forwarded_args),
            echo=echo,
        )
    return run_repo_script(repo_launcher.script, forwarded_args, echo=echo)


def list_directory_launchers(directory: Path, launchers: Dict[str, RepoLauncher]) -> None:
    print(MSG_DIR_COMMANDS.format(directory=directory), flush=True)
    for launcher in launchers.values():
        print(MSG_DIR_ITEM.format(launcher=launcher), flush=True)


def run_directory_launcher(directory: Path, argv: Sequence[str]) -> int:
    """Dispatch a category launcher to one of its leaf launchers."""
    launchers = discover_directory_launchers(directory)
    values = list(argv)
    if not values:
        list_directory_launchers(directory, launchers)
        return 2
    if values[0] in (COMMAND_HELP_SHORT, COMMAND_HELP_LONG, COMMAND_PROFILES):
        list_directory_launchers(directory, launchers)
        return 0

    repo_launcher = launchers.get(values[0])
    if repo_launcher is None:
        valid = LIST_ITEM_SEPARATOR.join(launchers)
        print(MSG_UNKNOWN_LAUNCHER.format(directory=directory, values=values, valid=valid), file=sys.stderr)
        return 2

    forwarded_args = values[1:]
    return run_repo_script(repo_launcher.script, forwarded_args, echo=not is_help_request(forwarded_args))


def list_profiles_command(args) -> int:
    print(MSG_LIST_COMMANDS, flush=True)
    print(MSG_BUILD_USAGE, flush=True)
    print(MSG_RUN_USAGE, flush=True)
    for launcher in args.repo_launchers.values():
        route = ROUTE_SEPARATOR.join(str(script) for script in (*launcher.route, launcher.script))
        print(MSG_ROUTE_ITEM.format(launcher=launcher, route=route), flush=True)
    return 0


def add_build_options(parser: argparse.ArgumentParser, *, allow_skip_build: bool = True) -> None:
    parser.add_argument(ARG_REPO_ROOT, type=Path, help=HELP_REPO_ROOT)
    parser.add_argument(ARG_PLATFORM, default=host_platform_name(), help=HELP_PLATFORM)
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


def add_common_options(parser: argparse.ArgumentParser) -> None:
    add_build_options(parser)
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


def make_parser(repo_launchers: Optional[Dict[str, RepoLauncher]] = None) -> argparse.ArgumentParser:
    if repo_launchers is None:
        repo_launchers = discover_repo_launchers()
    parser = argparse.ArgumentParser(description=MSG_LAUNCHER_DESC)
    subparsers = parser.add_subparsers(dest=ARG_COMMAND, required=True)

    build_parser = subparsers.add_parser(COMMAND_BUILD, help=MSG_BUILD_TARGET_HELP)
    add_build_options(build_parser, allow_skip_build=False)
    build_parser.add_argument(ARG_TARGETS, nargs=NARGS_ONE_OR_MORE, help=MSG_BUILD_TARGETS_HELP)
    build_parser.set_defaults(handler=build_command, skip_build=False)

    run_parser = subparsers.add_parser(COMMAND_RUN, help=MSG_RUN_TARGET_HELP)
    add_common_options(run_parser)
    run_parser.add_argument(ARG_BUILD_ONLY, action=STORE_TRUE, help=HELP_BUILD_ONLY)
    run_parser.add_argument(ARG_TARGET, help=MSG_TARGET_HELP)
    run_parser.set_defaults(handler=run_target_command)

    for launcher in repo_launchers.values():
        route = ROUTE_SEPARATOR.join(str(script) for script in (*launcher.route, launcher.script))
        subparsers.add_parser(launcher.command, help=MSG_FORWARD_THROUGH.format(route=route))

    profiles_parser = subparsers.add_parser(COMMAND_PROFILES, help=MSG_PROFILES_HELP)
    profiles_parser.set_defaults(handler=list_profiles_command, repo_launchers=repo_launchers)

    return parser


def split_application_args(argv: Sequence[str]) -> Tuple[List[str], List[str]]:
    values = list(argv)
    if COMMAND_SEPARATOR not in values:
        return values, []
    separator = values.index(COMMAND_SEPARATOR)
    return values[:separator], values[separator + 1 :]


def main(argv: Sequence[str]) -> int:
    repo_launchers = discover_repo_launchers()
    if argv and argv[0] in repo_launchers:
        launcher = repo_launchers[argv[0]]
        return run_discovered_launcher(launcher, argv[1:], echo=not is_help_request(argv[1:]))

    parser_args, application_args = split_application_args(argv)
    parser = make_parser(repo_launchers)
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
