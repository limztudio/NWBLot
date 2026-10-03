#!/usr/bin/env python3
"""Repository launcher package root.

Each functionality domain is a separated class in its own module; this
package root re-exports the public surface for ``import launcher`` /
``ROOT_LAUNCHER.*`` call sites.

Domains:
- launcher.models (LauncherModels): immutable data models.
- launcher.discovery (LauncherDiscovery): repo launcher discovery/routing.
- launcher.host (HostProbe): platform/arch probing and path layout.
- launcher.cmake_settings (CmakeSettings): CMake settings/cache/File API.
- launcher.build (BuildController): build orchestration/executable lookup.
- launcher.process (ProcessLauncher): process lifecycle/launch.
- launcher.profile (ProfileSessionController): logserver profile sessions.
- launcher.dispatch (DispatchRouter): build/run command dispatch.
- launcher.cli (LauncherCli): CLI construction and top-level routing.
- launcher.constants: shared literals.
- launcher.repository_windows_process: Windows process control (unchanged).
"""

from launcher.constants import *  # noqa: F401,F403
from launcher.models import (  # noqa: F401
    CMakeTargetInfo,
    LaunchSettings,
    LauncherModels,
    ProfileSession,
    RepoLauncher,
)
from launcher.discovery import LauncherDiscovery  # noqa: F401
from launcher.host import HostProbe  # noqa: F401
from launcher.cmake_settings import CmakeSettings  # noqa: F401
from launcher.build import BuildController  # noqa: F401
from launcher.process import ProcessLauncher  # noqa: F401
from launcher.profile import ProfileSessionController  # noqa: F401
from launcher.dispatch import DispatchRouter  # noqa: F401
from launcher.cli import LauncherCli  # noqa: F401
from launcher import repository_windows_process  # noqa: F401

from launcher.models import LaunchSettings  # noqa: F401
from launcher.models import CMakeTargetInfo  # noqa: F401
from launcher.models import RepoLauncher  # noqa: F401
from launcher.models import ProfileSession  # noqa: F401
from launcher.cmake_settings import repo_root  # noqa: F401
from launcher.discovery import launch_command_from_directory  # noqa: F401
from launcher.discovery import validate_launch_command  # noqa: F401
from launcher.discovery import discover_directory_launchers  # noqa: F401
from launcher.discovery import launcher_route  # noqa: F401
from launcher.discovery import discover_leaf_launchers  # noqa: F401
from launcher.discovery import discover_repo_launchers  # noqa: F401
from launcher.host import host_platform_name  # noqa: F401
from launcher.host import query_windows_native_machine_name  # noqa: F401
from launcher.host import windows_native_machine_name  # noqa: F401
from launcher.host import host_arch_name  # noqa: F401
from launcher.host import configure_preset_architecture  # noqa: F401
from launcher.host import executable_name  # noqa: F401
from launcher.host import default_configure_preset_name  # noqa: F401
from launcher.host import default_build_dir  # noqa: F401
from launcher.host import output_root  # noqa: F401
from launcher.host import target_default_executable_base_name  # noqa: F401
from launcher.cmake_settings import read_cmake_cache_value  # noqa: F401
from launcher.cmake_settings import infer_output_domain  # noqa: F401
from launcher.cmake_settings import cmake_command  # noqa: F401
from launcher.cmake_settings import format_command  # noqa: F401
from launcher.cmake_settings import run_checked  # noqa: F401
from launcher.cmake_settings import parse_define_entries  # noqa: F401
from launcher.cmake_settings import cmake_define_args  # noqa: F401
from launcher.cmake_settings import normalize_cache_bool  # noqa: F401
from launcher.cmake_settings import cache_matches_required_defines  # noqa: F401
from launcher.cmake_settings import merged_required_defines  # noqa: F401
from launcher.cmake_settings import profile_required_defines  # noqa: F401
from launcher.cmake_settings import file_api_query_path  # noqa: F401
from launcher.cmake_settings import file_api_reply_dir  # noqa: F401
from launcher.cmake_settings import ensure_file_api_query  # noqa: F401
from launcher.cmake_settings import latest_file_api_index  # noqa: F401
from launcher.cmake_settings import file_api_has_reply  # noqa: F401
from launcher.cmake_settings import read_json  # noqa: F401
from launcher.cmake_settings import load_cmake_target_info  # noqa: F401
from launcher.cmake_settings import resolve_repo_root  # noqa: F401
from launcher.cmake_settings import resolve_launch_settings  # noqa: F401
from launcher.cmake_settings import refresh_launch_settings  # noqa: F401
from launcher.cmake_settings import configure_command  # noqa: F401
from launcher.cmake_settings import maybe_configure  # noqa: F401
from launcher.build import build_targets  # noqa: F401
from launcher.build import build_target  # noqa: F401
from launcher.build import build_profile_targets  # noqa: F401
from launcher.build import resolve_executable_path  # noqa: F401
from launcher.build import resolve_working_directory  # noqa: F401
from launcher.cmake_settings import resolve_path  # noqa: F401
from launcher.build import build_environment  # noqa: F401
from launcher.build import normalize_application_args  # noqa: F401
from launcher.process import stop_existing_process  # noqa: F401
from launcher.process import validate_launch_paths  # noqa: F401
from launcher.process import terminate_process  # noqa: F401
from launcher.process import wait_for_process_exit  # noqa: F401
from launcher.profile import choose_free_tcp_port  # noqa: F401
from launcher.profile import ensure_tcp_port_available  # noqa: F401
from launcher.profile import resolve_profile_log_port  # noqa: F401
from launcher.profile import wait_for_tcp_port  # noqa: F401
from launcher.profile import profile_client_args  # noqa: F401
from launcher.profile import start_profile_session  # noqa: F401
from launcher.process import launch_process  # noqa: F401
from launcher.process import launch_with_optional_profile  # noqa: F401
from launcher.build import build_command  # noqa: F401
from launcher.dispatch import run_target_command  # noqa: F401
from launcher.process import run_target_launcher  # noqa: F401
from launcher.process import run_repo_script  # noqa: F401
from launcher.dispatch import is_help_request  # noqa: F401
from launcher.discovery import run_discovered_launcher  # noqa: F401
from launcher.discovery import list_directory_launchers  # noqa: F401
from launcher.discovery import run_directory_launcher  # noqa: F401
from launcher.discovery import list_profiles_command  # noqa: F401
from launcher.cli import add_build_options  # noqa: F401
from launcher.cli import add_common_options  # noqa: F401
from launcher.cli import make_parser  # noqa: F401
from launcher.cli import split_application_args  # noqa: F401
from launcher.cli import main  # noqa: F401

__all__ = [
    'ARCH_X64_LITERAL',
    'ARCH_ARM64_LITERAL',
    'CONFIGURATIONS',
    'SUPPORTED_ARCHITECTURES',
    'WINDOWS_IMAGE_FILE_MACHINE_AMD64',
    'WINDOWS_IMAGE_FILE_MACHINE_ARM64',
    'WINDOWS_NATIVE_MACHINE_NAMES',
    'DEFAULT_CONFIG',
    'DEFAULT_DOMAIN',
    'DEFAULT_BUILD_JOBS',
    'LAUNCHER_SEARCH_ROOTS',
    'LAUNCHER_SCRIPT_NAME',
    'RESERVED_LAUNCH_COMMANDS',
    'PROFILE_LOGSERVER_TARGET',
    'LOGSERVER_LABEL',
    'PROFILE_LOGSERVER_EXECUTABLE',
    'PROFILE_LOG_ADDRESS',
    'PROFILE_LOGSERVER_TIMEOUT_SECONDS',
    'PROFILE_LOGSERVER_TERMINATE_TIMEOUT_SECONDS',
    'APPLICATION_GRACEFUL_STOP_TIMEOUT_SECONDS',
    'APPLICATION_FORCED_STOP_TIMEOUT_SECONDS',
    'PROFILE_LOG_HOST',
    'PROFILE_LOG_PORT_AUTO',
    'PROFILE_LOG_PORT_MIN',
    'PROFILE_LOG_PORT_MAX',
    'PROFILE_LOG_DRY_RUN_PORT',
    'PROFILE_LOG_CONNECT_TIMEOUT_SECONDS',
    'PROFILE_LOG_READY_POLL_SECONDS',
    'OS_WINDOWS',
    'PRESET_NAME_FORMAT',
    'PRESET_NAME_DOMAIN_FORMAT',
    'PRESET_PREFIX_FORMAT',
    'FILE_API_OBJECTS_KEY',
    'PROFILE_REQUIRED_DEFINES',
    'ARCH_X64',
    'ARCH_ARM64',
    'ARCH_AMD64_ALIAS',
    'ARCH_X86_64_ALIAS',
    'ARCH_X86_64_DASH_ALIAS',
    'ARCH_AARCH64_ALIAS',
    'PLATFORM_WINDOWS',
    'PLATFORM_LINUX',
    'PLATFORM_DARWIN',
    'PLATFORM_WINDOWS_SYSTEM',
    'PLATFORM_LINUX_SYSTEM',
    'PLATFORM_DARWIN_SYSTEM',
    'ENGINE_DOMAIN',
    'NWB_TARGET_PREFIX',
    'CMAKE_CACHE_FILE',
    'CMAKE_OUTPUT_DOMAIN_KEY',
    'CMAKE_COMMAND_ENV',
    'CMAKE_DEFAULT_EXECUTABLE',
    'CMAKE_TOOL_VENV_DIR',
    'CMAKE_LOCAL_BIN_WINDOWS',
    'CMAKE_LOCAL_BIN_POSIX',
    'CMAKE_BUILD_ROOT_DIR',
    'CMAKE_BUILD_SUBDIR',
    'EXEC_OUTPUT_ROOT_DIR',
    'EXEC_WINDOWS_SUFFIX',
    'FILE_API_DIR_CMAKE',
    'FILE_API_DIR_API',
    'FILE_API_DIR_V1',
    'FILE_API_DIR_QUERY',
    'FILE_API_DIR_REPLY',
    'FILE_API_CODEMODEL',
    'FILE_API_INDEX_GLOB',
    'FILE_API_KIND_KEY',
    'FILE_API_KIND_CODEMODEL',
    'FILE_API_VERSION_KEY',
    'FILE_API_VERSION_MAJOR',
    'FILE_API_CODEMODEL_VERSION',
    'FILE_API_JSON_FILE_KEY',
    'FILE_API_CONFIGURATIONS_KEY',
    'FILE_API_NAME_KEY',
    'FILE_API_TARGETS_KEY',
    'FILE_API_ARTIFACTS_KEY',
    'FILE_API_PATH_KEY',
    'FILE_API_TYPE_KEY',
    'FILE_API_TARGET_EXECUTABLE',
    'COMMAND_SEPARATOR',
    'COMMAND_BUILD',
    'COMMAND_RUN',
    'COMMAND_PROFILES',
    'COMMAND_HELP_SHORT',
    'COMMAND_HELP_LONG',
    'ROUTE_SEPARATOR',
    'LOG_PREFIX',
    'CWD_PREFIX',
    'LAUNCH_COMMAND_PATTERN',
    'LAUNCH_COMMAND_NAME_UNDERSCORE',
    'LAUNCH_COMMAND_NAME_HYPHEN',
    'LAUNCHER_GLOB_SUFFIX',
    'KIND_CATEGORY',
    'KIND_DIRECTORY',
    'CONFIGURE_ALWAYS',
    'CONFIGURE_NEVER',
    'CONFIGURE_AUTO',
    'DEFINE_ACTION',
    'DEFINE_METAVAR',
    'STORE_TRUE',
    'NARGS_ONE_OR_MORE',
    'EMPTY_STRING',
    'ARG_JOIN_SEPARATOR',
    'DEFAULT_DOMAIN_FALLBACK',
    'CACHE_FILE_ENCODING',
    'CACHE_FILE_ERRORS',
    'CACHE_FILE_MODE',
    'CACHE_KEY_SEPARATOR',
    'CACHE_ENTRY_SEPARATOR',
    'CACHE_LINE_END',
    'CMAKE_DEFINE_EQUALS',
    'CMAKE_DEFINE_PREFIX',
    'CMAKE_BOOL_TRUE_TOKENS',
    'CMAKE_BOOL_FALSE_TOKENS',
    'WINDOWS_KERNEL32',
    'WINDOWS_WOW64_PROC2',
    'WINDOWS_ENV_ARCH6432',
    'WINDOWS_ENV_ARCH',
    'PRESET_ARCH_SEPARATOR',
    'PRESET_TOOLCHAIN',
    'OPTION_WITH_PROFILE',
    'OPTION_RUN_SECONDS',
    'STOP_KIND_FORCED',
    'STOP_KIND_GRACEFUL',
    'PKILL_COMMAND',
    'PKILL_FOLLOW_FLAG',
    'GPUDBG_FLAG',
    'PROFILE_CLIENT_ADDRESS_FLAG',
    'PROFILE_CLIENT_PORT_FLAG',
    'CMAKE_PRESET_FLAG',
    'CMAKE_SOURCE_FLAG',
    'CMAKE_BINARY_FLAG',
    'CMAKE_BUILD_FLAG',
    'CMAKE_TARGET_FLAG',
    'CMAKE_CONFIG_FLAG',
    'CMAKE_PARALLEL_FLAG',
    'LIST_ITEM_SEPARATOR',
    'MSG_DUPLICATE_COMMAND',
    'MSG_DISAMBIGUATE',
    'MSG_INVALID_COMMAND',
    'MSG_COMMAND_CONFLICT',
    'MSG_MISSING_LAUNCHER_PREFIX',
    'MSG_UNSUPPORTED_ARCH',
    'MSG_ARCH_PRESET_CONFLICT',
    'MSG_CONFIGURE_REQUIRED',
    'MSG_NO_TARGETS',
    'MSG_NOT_EXECUTABLE',
    'MSG_NO_METADATA',
    'MSG_NO_PKILL',
    'MSG_MISSING_EXECUTABLE',
    'MSG_MISSING_WORKDIR',
    'MSG_PORT_BUSY',
    'MSG_PORT_RANGE',
    'MSG_LOGSERVER_EXITED',
    'MSG_LOGSERVER_TIMEOUT_SUFFIX',
    'MSG_LOGSERVER_NO_ACCEPT',
    'MSG_POSITIVE_TIMEOUT',
    'MSG_LAUNCHED_LOGSERVER',
    'MSG_LAUNCHED_APP',
    'MSG_STOPPED_APP',
    'MSG_STOPPING',
    'MSG_KILLING',
    'MSG_RUN_GRACEFUL',
    'MSG_RUN_FORCED',
    'MSG_RUN_REQUEST',
    'MSG_NO_EXIT_STATUS',
    'MSG_LEAVING_BOTH',
    'MSG_LEAVING_APP',
    'MSG_UNKNOWN_LAUNCHER',
    'MSG_BUILD_USAGE',
    'MSG_RUN_USAGE',
    'MSG_CMAKE_DEFINE_USAGE',
    'MSG_CMAKE_DEFINE_EMPTY',
    'ARG_REPO_ROOT',
    'ARG_PLATFORM',
    'ARG_ARCH',
    'ARG_DOMAIN',
    'ARG_CONFIGURE_PRESET',
    'ARG_BUILD_DIR',
    'ARG_CMAKE',
    'ARG_JOBS',
    'ARG_CONFIGURE',
    'ARG_DEFINE_SHORT',
    'ARG_DEFINE_LONG',
    'ARG_DEFINE_DEST',
    'ARG_SKIP_BUILD',
    'ARG_BUILD_ONLY',
    'ARG_DRY_RUN',
    'ARG_WORKING_DIRECTORY',
    'ARG_EXECUTABLE',
    'ARG_EXECUTABLE_NAME',
    'ARG_KILL_EXISTING',
    'ARG_DETACH',
    'ARG_RUN_SECONDS',
    'ARG_WITH_PROFILE',
    'ARG_PROFILE_LOG_ADDRESS',
    'ARG_PROFILE_LOG_PORT',
    'ARG_PROFILE_LOGSERVER_TARGET',
    'ARG_PROFILE_LOGSERVER_NAME',
    'ARG_PROFILE_LOGSERVER_EXECUTABLE',
    'ARG_PROFILE_LOGSERVER_TIMEOUT',
    'ARG_PROFILE_LOGSERVER_ARG',
    'ARG_PROFILE_LOGSERVER_ARGS_DEST',
    'ARG_TARGET',
    'ARG_TARGETS',
    'ARG_COMMAND',
    'MSG_LIST_COMMANDS',
    'MSG_ROUTE_ITEM',
    'MSG_DIR_COMMANDS',
    'MSG_DIR_ITEM',
    'MSG_FORWARD_THROUGH',
    'MSG_BUILD_TARGET_HELP',
    'MSG_RUN_TARGET_HELP',
    'MSG_PROFILES_HELP',
    'MSG_LAUNCHER_DESC',
    'MSG_TARGET_HELP',
    'MSG_BUILD_TARGETS_HELP',
    'MSG_BUILD_APPLICATION_ARGS',
    'MSG_BUILD_ONLY_SKIP_BUILD',
    'CONFIGURE_CHOICES',
    'MAIN_ENTRY',
    'HELP_REPO_ROOT',
    'HELP_PLATFORM',
    'HELP_ARCH',
    'HELP_DOMAIN',
    'HELP_CONFIGURE_PRESET',
    'HELP_BUILD_DIR',
    'HELP_CMAKE',
    'HELP_JOBS',
    'HELP_CONFIGURE',
    'HELP_SKIP_BUILD',
    'HELP_BUILD_ONLY',
    'HELP_DRY_RUN',
    'HELP_BUILD_DRY_RUN',
    'HELP_WORKING_DIRECTORY',
    'HELP_EXECUTABLE',
    'HELP_EXECUTABLE_NAME',
    'HELP_KILL_EXISTING',
    'HELP_DETACH',
    'HELP_RUN_SECONDS',
    'HELP_WITH_PROFILE',
    'HELP_PROFILE_LOG_ADDRESS',
    'HELP_PROFILE_LOG_PORT',
    'HELP_PROFILE_LOGSERVER_TARGET',
    'HELP_PROFILE_LOGSERVER_NAME',
    'HELP_PROFILE_LOGSERVER_EXECUTABLE',
    'HELP_PROFILE_LOGSERVER_TIMEOUT',
    'HELP_PROFILE_LOGSERVER_ARG',
    'LaunchSettings',
    'CMakeTargetInfo',
    'RepoLauncher',
    'ProfileSession',
    'repo_root',
    'launch_command_from_directory',
    'validate_launch_command',
    'discover_directory_launchers',
    'launcher_route',
    'discover_leaf_launchers',
    'discover_repo_launchers',
    'host_platform_name',
    'query_windows_native_machine_name',
    'windows_native_machine_name',
    'host_arch_name',
    'configure_preset_architecture',
    'executable_name',
    'default_configure_preset_name',
    'default_build_dir',
    'output_root',
    'target_default_executable_base_name',
    'read_cmake_cache_value',
    'infer_output_domain',
    'cmake_command',
    'format_command',
    'run_checked',
    'parse_define_entries',
    'cmake_define_args',
    'normalize_cache_bool',
    'cache_matches_required_defines',
    'merged_required_defines',
    'profile_required_defines',
    'file_api_query_path',
    'file_api_reply_dir',
    'ensure_file_api_query',
    'latest_file_api_index',
    'file_api_has_reply',
    'read_json',
    'load_cmake_target_info',
    'resolve_repo_root',
    'resolve_launch_settings',
    'refresh_launch_settings',
    'configure_command',
    'maybe_configure',
    'build_targets',
    'build_target',
    'build_profile_targets',
    'resolve_executable_path',
    'resolve_working_directory',
    'resolve_path',
    'build_environment',
    'normalize_application_args',
    'stop_existing_process',
    'validate_launch_paths',
    'terminate_process',
    'wait_for_process_exit',
    'choose_free_tcp_port',
    'ensure_tcp_port_available',
    'resolve_profile_log_port',
    'wait_for_tcp_port',
    'profile_client_args',
    'start_profile_session',
    'launch_process',
    'launch_with_optional_profile',
    'build_command',
    'run_target_command',
    'run_target_launcher',
    'run_repo_script',
    'is_help_request',
    'run_discovered_launcher',
    'list_directory_launchers',
    'run_directory_launcher',
    'list_profiles_command',
    'add_build_options',
    'add_common_options',
    'make_parser',
    'split_application_args',
    'main',
    'LauncherDiscovery',
    'HostProbe',
    'CmakeSettings',
    'BuildController',
    'ProcessLauncher',
    'ProfileSessionController',
    'DispatchRouter',
    'LauncherCli',
    'LauncherModels',
    'repository_windows_process',
]
