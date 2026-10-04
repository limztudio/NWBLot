#!/usr/bin/env python3
"""Find and qualify the compiler host's matching LLVM C API library."""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

from llvm_api import LlvmApi


def compiler_version(compiler: Path) -> tuple[int, int, int]:
    result = subprocess.run([str(compiler), "--version"], check=True, capture_output=True, text=True)
    match = re.search(r"\bclang version (\d+)\.(\d+)\.(\d+)", result.stdout)
    if not match:
        raise ValueError(f"Cannot identify the LLVM version of {compiler}: {result.stdout.strip()}")
    return tuple(int(value) for value in match.groups())


def library_version(path: Path, expected: tuple[int, int, int]) -> tuple[int, int, int]:
    directory = os.add_dll_directory(str(path.parent)) if os.name == "nt" else None
    try:
        return LlvmApi(path, expected).version
    finally:
        if directory is not None:
            directory.close()


def library_candidates(compiler: Path, major: int) -> list[Path]:
    compiler = compiler.resolve()
    directories = [compiler.parent, compiler.parent.parent / "lib", compiler.parent.parent / "lib64"]
    llvm_config = compiler.parent / ("llvm-config.exe" if os.name == "nt" else "llvm-config")
    if llvm_config.is_file():
        result = subprocess.run([str(llvm_config), "--libdir"], check=True, capture_output=True, text=True)
        directories.insert(0, Path(result.stdout.strip()))
    if sys.platform.startswith("linux"):
        directories += [Path(f"/usr/lib/llvm-{major}/lib"), Path("/usr/lib"), Path("/usr/lib64")]
        directories += [path for path in Path("/usr/lib").glob("*-linux-gnu") if path.is_dir()]
    patterns = ["LLVM-C.dll"] if os.name == "nt" else [f"libLLVM-{major}.so*", "libLLVM.so*", "libLLVM*.dylib"]
    candidates = []
    for directory in directories:
        for pattern in patterns:
            for path in sorted(directory.glob(pattern)):
                resolved = path.resolve()
                if resolved.is_file() and resolved not in candidates:
                    candidates.append(resolved)
    return candidates


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--c-compiler", type=Path, required=True)
    parser.add_argument("--library", type=Path)
    args = parser.parse_args()
    version = compiler_version(args.compiler)
    if compiler_version(args.c_compiler) != version:
        raise ValueError("String obfuscation requires matching C and C++ Clang versions.")
    candidates = [args.library.resolve()] if args.library else library_candidates(args.compiler, version[0])
    failures = []
    for candidate in candidates:
        try:
            actual = library_version(candidate, version)
        except (OSError, AttributeError, RuntimeError) as error:
            failures.append(f"{candidate}: {error}")
            continue
        if actual != version:
            failures.append(f"{candidate}: LLVM {'.'.join(map(str, actual))}, expected {'.'.join(map(str, version))}")
            continue
        print(json.dumps({"library": str(candidate), "version": ".".join(map(str, version))}))
        return 0
    details = "\n".join(failures) or "No LLVM C API shared library was found beside the compiler."
    raise ValueError(
        "String obfuscation requires the compiler host's matching LLVM C API shared library. "
        "Install it with the Clang toolchain or set NWB_LLVM_C_LIBRARY to its path.\n" + details
    )


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"String literal configuration failed: {error}", file=sys.stderr)
        raise SystemExit(1)
