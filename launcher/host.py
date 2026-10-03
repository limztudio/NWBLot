#!/usr/bin/env python3
"""HostProbe: Host platform/architecture probing and preset path layout."""

from __future__ import annotations

import ctypes
import platform
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple
from launcher.constants import (
    ARCH_AARCH64_ALIAS,
    ARCH_AMD64_ALIAS,
    ARCH_ARM64,
    ARCH_X64,
    ARCH_X86_64_ALIAS,
    ARCH_X86_64_DASH_ALIAS,
    CMAKE_BUILD_ROOT_DIR,
    CMAKE_BUILD_SUBDIR,
    DEFAULT_DOMAIN,
    ENGINE_DOMAIN,
    EXEC_OUTPUT_ROOT_DIR,
    EXEC_WINDOWS_SUFFIX,
    MSG_UNSUPPORTED_ARCH,
    MSG_WINDOWS_ARCH_API_REQUIRED,
    MSG_WINDOWS_ARCH_QUERY_FAILED,
    MSG_WINDOWS_NATIVE_MACHINE_UNSUPPORTED,
    NWB_TARGET_PREFIX,
    PLATFORM_DARWIN,
    PLATFORM_DARWIN_SYSTEM,
    PLATFORM_LINUX,
    PLATFORM_LINUX_SYSTEM,
    PLATFORM_WINDOWS,
    PLATFORM_WINDOWS_SYSTEM,
    PRESET_ARCH_SEPARATOR,
    PRESET_NAME_DOMAIN_FORMAT,
    PRESET_NAME_FORMAT,
    PRESET_TOOLCHAIN,
    SUPPORTED_ARCHITECTURES,
    WINDOWS_KERNEL32,
    WINDOWS_NATIVE_MACHINE_NAMES,
    WINDOWS_WOW64_PROC2,
)


class HostProbe:
    """Host platform/architecture probing and preset path layout."""

    @staticmethod
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

    @staticmethod
    def query_windows_native_machine_name() -> str:
        kernel32 = ctypes.WinDLL(WINDOWS_KERNEL32, use_last_error=True)
        is_wow64_process2 = getattr(kernel32, WINDOWS_WOW64_PROC2, None)
        if is_wow64_process2 is None:
            raise SystemExit(MSG_WINDOWS_ARCH_API_REQUIRED)

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
            raise SystemExit(MSG_WINDOWS_ARCH_QUERY_FAILED.format(error=ctypes.get_last_error()))
        machine = WINDOWS_NATIVE_MACHINE_NAMES.get(native_machine.value)
        if machine is None:
            raise SystemExit(MSG_WINDOWS_NATIVE_MACHINE_UNSUPPORTED.format(machine=native_machine.value))
        return machine

    @staticmethod
    def host_arch_name(machine_name: Optional[str] = None) -> str:
        if machine_name is None:
            machine_name = HostProbe.query_windows_native_machine_name() if platform.system() == PLATFORM_WINDOWS_SYSTEM else platform.machine()
        machine = machine_name.lower()
        if machine in (ARCH_X64, ARCH_AMD64_ALIAS, ARCH_X86_64_ALIAS, ARCH_X86_64_DASH_ALIAS):
            return ARCH_X64
        if machine in (ARCH_ARM64, ARCH_AARCH64_ALIAS):
            return ARCH_ARM64
        raise SystemExit(MSG_UNSUPPORTED_ARCH.format(machine=machine))

    @staticmethod
    def configure_preset_architecture(preset_name: str) -> Optional[str]:
        return next((arch for arch in SUPPORTED_ARCHITECTURES if preset_name.endswith(PRESET_ARCH_SEPARATOR + arch)), None)

    @staticmethod
    def executable_name(base_name: str, platform_name: str) -> str:
        return base_name + EXEC_WINDOWS_SUFFIX if platform_name == PLATFORM_WINDOWS else base_name

    @staticmethod
    def default_configure_preset_name(platform_name: str, domain: str, arch: str) -> str:
        if domain == DEFAULT_DOMAIN:
            return PRESET_NAME_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN, arch=arch)
        return PRESET_NAME_DOMAIN_FORMAT.format(platform_name=platform_name, toolchain=PRESET_TOOLCHAIN, domain=domain, arch=arch)

    @staticmethod
    def default_build_dir(root: Path, platform_name: str, domain: str, arch: str) -> Path:
        import launcher as _facade
        return root / CMAKE_BUILD_ROOT_DIR / CMAKE_BUILD_SUBDIR / _facade.default_configure_preset_name(platform_name, domain, arch)

    @staticmethod
    def output_root(root: Path, platform_name: str, arch: str, domain: str) -> Path:
        base = root / EXEC_OUTPUT_ROOT_DIR / platform_name / arch
        if domain == ENGINE_DOMAIN:
            return base
        return base / domain

    @staticmethod
    def target_default_executable_base_name(target: str) -> str:
        if target.startswith(NWB_TARGET_PREFIX):
            return target[len(NWB_TARGET_PREFIX):]
        return target


host_platform_name = HostProbe.host_platform_name
query_windows_native_machine_name = HostProbe.query_windows_native_machine_name
host_arch_name = HostProbe.host_arch_name
configure_preset_architecture = HostProbe.configure_preset_architecture
executable_name = HostProbe.executable_name
default_configure_preset_name = HostProbe.default_configure_preset_name
default_build_dir = HostProbe.default_build_dir
output_root = HostProbe.output_root
target_default_executable_base_name = HostProbe.target_default_executable_base_name