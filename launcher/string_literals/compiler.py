#!/usr/bin/env python3
"""Compile optimized Clang bitcode, transform literals, then emit the object."""

from __future__ import annotations

import argparse
import importlib
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".c++", ".C"}
FRONTEND_VALUE_OPTIONS = {
    "-D", "-U", "-I", "-isystem", "-iquote", "-idirafter", "-include",
    "-imacros", "-include-pch", "-iprefix", "-iwithprefix", "-iwithprefixbefore",
    "-isystem-after", "-iframework", "-iframeworkwithsysroot", "-ivfsoverlay",
    "-MF", "-MT", "-MQ", "-MJ", "-dependency-file", "-serialize-diagnostics", "-x", "-std",
}
DRIVER_VALUE_OPTIONS = {
    "-target", "--target", "-arch", "-isysroot", "--sysroot", "-resource-dir",
    "-B", "-gcc-toolchain", "--gcc-toolchain", "-Xassembler", "-mllvm",
    "-Xpreprocessor", "-Xclang", "-fdebug-compilation-dir", "-fcoverage-compilation-dir",
}
FRONTEND_SINGLE_OPTIONS = {
    "-MD", "-MMD", "-MP", "-MG", "-fno-rtti", "-frtti", "-fno-exceptions",
    "-fexceptions", "-nostdinc", "-nostdinc++", "-undef", "-pedantic", "-pedantic-errors",
}
FRONTEND_PREFIX_OPTIONS = (
    "-D", "-U", "-I", "-std=", "-MF", "-MT", "-MQ", "-MJ", "-fdiagnostics-",
    "-isystem", "-iquote", "-idirafter", "-include", "-imacros", "-iframework",
    "-fmacro-prefix-map=", "-fmodules-cache-path=", "-fmodule-file=",
)


def effective_argument(argument: str) -> str:
    return argument[len("/clang:"):] if argument.startswith("/clang:") else argument


def split_gnu_response(text: str) -> list[str]:
    """LLVM GNU response escaping applies inside both kinds of quoted text."""
    result = []
    index = 0
    whitespace = " \t\r\n"
    while index < len(text):
        while index < len(text) and text[index] in whitespace:
            index += 1
        if index == len(text):
            break
        word = []
        while index < len(text) and text[index] not in whitespace:
            character = text[index]
            if character == "\\" and index + 1 < len(text):
                index += 1
                word.append(text[index])
            elif character in {'"', "'"}:
                quote = character
                index += 1
                while index < len(text) and text[index] != quote:
                    if text[index] == "\\" and index + 1 < len(text):
                        index += 1
                    word.append(text[index])
                    index += 1
                if index == len(text):
                    raise ValueError("Unterminated quote in response file")
            else:
                word.append(character)
            index += 1
        result.append("".join(word))
    return result


def quote_gnu_response(arguments: Sequence[str]) -> str:
    return " ".join('"' + argument.replace("\\", "\\\\").replace('"', '\\"') + '"' for argument in arguments)


def split_windows_response(text: str) -> list[str]:
    """LLVM's Windows response quoting: backslashes escape only double quotes."""
    result = []
    index = 0
    while index < len(text):
        while index < len(text) and text[index].isspace():
            index += 1
        if index == len(text):
            break
        word = []
        quoted = False
        while index < len(text) and (quoted or not text[index].isspace()):
            if text[index] == "\\":
                start = index
                while index < len(text) and text[index] == "\\":
                    index += 1
                count = index - start
                if index < len(text) and text[index] == '"':
                    word.append("\\" * (count // 2))
                    if count % 2:
                        word.append('"')
                        index += 1
                        continue
                else:
                    word.append("\\" * count)
                    continue
            if text[index] == '"':
                if quoted and index + 1 < len(text) and text[index + 1] == '"':
                    word.append('"')
                    index += 2
                    continue
                quoted = not quoted
            else:
                word.append(text[index])
            index += 1
        if quoted:
            raise ValueError("Unterminated quote in response file")
        result.append("".join(word))
    return result


def driver_response_quoting(tool: str, arguments: Sequence[str] = ()) -> str:
    name = Path(tool).stem.lower()
    return "windows" if name.endswith(("clang-cl", "lld-link")) or name == "link" or "--driver-mode=cl" in arguments else "posix"


def expand_response_files(arguments: Sequence[str], quoting: Optional[str] = None) -> list[str]:
    mode = quoting or "posix"
    for argument in arguments:
        if argument.startswith("--rsp-quoting="):
            mode = argument.split("=", 1)[1]
    if mode not in {"windows", "posix"}:
        raise ValueError(f"Unsupported response quoting: {mode}")

    def expand(items: Sequence[str], ancestors: tuple[Path, ...]) -> list[str]:
        expanded = []
        for argument in items:
            if not argument.startswith("@"):
                expanded.append(argument)
                continue
            path = Path(argument[1:]).resolve()
            if path in ancestors or len(ancestors) >= 64:
                raise ValueError(f"Recursive response file: {path}")
            data = path.read_bytes()
            encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
            text = data.decode(encoding)
            tokens = split_windows_response(text) if mode == "windows" else split_gnu_response(text)
            expanded.extend(expand(tokens, ancestors + (path,)))
        return expanded

    return expand(arguments, ())


def run_tool(command: Sequence[str], response_path: Path, prefix: Sequence[str] = ()) -> subprocess.CompletedProcess:
    """Keep expanded compiler/linker arguments below host command-line limits."""
    mode = driver_response_quoting(command[0], command[1:])
    controls = [argument for argument in command[1:] if argument.startswith("--rsp-quoting=")]
    if controls:
        mode = controls[-1].split("=", 1)[1]
    if mode not in {"windows", "posix"}:
        raise ValueError(f"Unsupported response quoting: {mode}")
    arguments = [argument for argument in command[1:] if not argument.startswith("--rsp-quoting=")]
    text = subprocess.list2cmdline(arguments) if mode == "windows" else quote_gnu_response(arguments)
    response_path.write_text(text + "\n", encoding="utf-8")
    return subprocess.run([*prefix, command[0], *controls, "@" + str(response_path.resolve())], check=False)


@dataclass(frozen=True)
class CompilePlan:
    compiler: str
    frontend: tuple[str, ...]
    backend: tuple[str, ...]
    output: Path
    clang_cl: bool


def compile_plan(command: Sequence[str]) -> Optional[CompilePlan]:
    if not command:
        raise ValueError("Missing compiler command after --")
    compiler = command[0]
    arguments = expand_response_files(command[1:], driver_response_quoting(compiler, command[1:]))
    clang_cl = Path(compiler).stem.lower().endswith("clang-cl") or "--driver-mode=cl" in arguments
    source_indices = set()
    frontend = []
    backend = []
    output = None
    language = None
    dependency = False
    dependency_path = False
    dependency_target = False
    object_compile = False
    passthrough = False
    unsupported = None
    index = 0
    while index < len(arguments):
        raw = arguments[index]
        argument = effective_argument(raw)
        if argument in {"-o", "/Fo"}:
            if index + 1 == len(arguments) or output is not None:
                raise ValueError("Object compilation requires exactly one output path")
            output = Path(effective_argument(arguments[index + 1]))
            index += 2
            continue
        if (argument.startswith("-o") and len(argument) > 2) or (clang_cl and argument.startswith("/Fo") and len(argument) > 3):
            if output is not None:
                raise ValueError("Object compilation requires exactly one output path")
            output = Path(argument[3:] if argument.startswith("/Fo") else argument[2:])
            index += 1
            continue
        if argument in {"-c", "/c"}:
            object_compile = True
            index += 1
            continue
        if argument in {"-E", "-M", "-MM", "-fsyntax-only", "--version", "-dumpversion", "-dumpmachine", "-###"} or (clang_cl and argument in {"/E", "/EP", "/P", "/?"}):
            passthrough = True
        if argument in {"-S", "-emit-llvm", "-save-temps", "-save-temps=obj"} or argument.startswith(("-flto", "-fmodule-output", "-fmodules-ts")):
            unsupported = argument
        if argument in FRONTEND_VALUE_OPTIONS | DRIVER_VALUE_OPTIONS:
            if index + 1 == len(arguments):
                raise ValueError(f"Missing value for {argument}")
            value = effective_argument(arguments[index + 1])
            pair = arguments[index:index + 2]
            frontend.extend(pair)
            if argument == "-x":
                language = value
            elif argument == "-MF":
                dependency_path = True
            elif argument in {"-MT", "-MQ"}:
                dependency_target = True
            elif argument == "-Xclang" and value in {"-emit-pch", "-emit-pth"}:
                passthrough = True
            if argument == "-Xclang" and value in {"-include-pch", "-include", "-imacros"}:
                if index + 3 >= len(arguments) or effective_argument(arguments[index + 2]) != "-Xclang":
                    raise ValueError(f"Unsupported paired frontend option: {value}")
                frontend.extend(arguments[index + 2:index + 4])
                index += 4
                continue
            if argument not in FRONTEND_VALUE_OPTIONS:
                backend.extend(pair)
            index += 2
            continue
        if clang_cl and argument.startswith(("/Tc", "/Tp")):
            if len(argument) == 3:
                if index + 1 == len(arguments):
                    raise ValueError(f"Missing source for {argument}")
                frontend.extend(arguments[index:index + 2])
                source_indices.add(index + 1)
                index += 2
            else:
                frontend.append(raw)
                source_indices.add(index)
                index += 1
            continue
        is_source = Path(argument).suffix in SOURCE_SUFFIXES and not argument.startswith("-")
        if not is_source and language in {"c", "c++", "objective-c", "objective-c++"}:
            is_source = not argument.startswith(("-", "/")) or Path(argument).is_file()
        if is_source:
            source_indices.add(index)
            frontend.append(raw)
            index += 1
            continue
        frontend.append(raw)
        if argument in {"-MD", "-MMD"}:
            dependency = True
        if argument.startswith("-MF"):
            dependency_path = True
        if argument.startswith(("-MT", "-MQ")):
            dependency_target = True
        front_only = argument in FRONTEND_SINGLE_OPTIONS or argument.startswith(FRONTEND_PREFIX_OPTIONS)
        front_only = front_only or argument.startswith("-W") or argument == "-w"
        if clang_cl:
            front_only = front_only or argument.startswith(("/D", "/U", "/I", "/FI", "/Yu", "/Yc", "/Fp", "/std:", "/showIncludes", "/sourceDependencies", "/W", "/external:")) or argument in {"/TC", "/TP", "/WX", "/WX-", "/w"}
            if argument.startswith("/Yc"):
                passthrough = True
            if argument in {"/D", "/U", "/I", "/FI", "/Yu", "/Fp", "/external:I", "/sourceDependencies"} or argument.startswith("/sourceDependencies:"):
                if index + 1 == len(arguments):
                    raise ValueError(f"Missing value for {argument}")
                frontend.append(arguments[index + 1])
                index += 2
                continue
        if not front_only:
            backend.append(raw)
        index += 1
    if passthrough or (language is not None and language not in {"c", "c++"}):
        return None
    if not source_indices:
        if object_compile and language in {"c", "c++"}:
            raise ValueError("Unable to identify the C/C++ source input")
        return None
    if unsupported:
        raise ValueError(f"Unsupported string-literal compilation mode: {unsupported}")
    if not object_compile or len(source_indices) != 1:
        raise ValueError("String-literal transformation requires a single C/C++ object compilation")
    if output is None:
        raise ValueError("String-literal transformation requires an explicit object output")
    if str(output) == "-" or output.suffix.lower() not in {".o", ".obj"}:
        raise ValueError(f"Unsupported object output: {output}")
    if dependency:
        prefix = "/clang:" if clang_cl else ""
        if not dependency_path:
            frontend.extend((prefix + "-MF", prefix + str(output.with_suffix(".d"))))
        if not dependency_target:
            frontend.extend((prefix + "-MT", prefix + str(output)))
    return CompilePlan(compiler, tuple(frontend), tuple(backend), output, clang_cl)


def stage_commands(plan: CompilePlan, input_bitcode: Path, encoded_bitcode: Path, object_path: Path) -> tuple[list[str], list[str]]:
    if plan.clang_cl:
        frontend = [plan.compiler, *plan.frontend, "/c", "/clang:-emit-llvm", "/Fo" + str(input_bitcode)]
        backend = [plan.compiler, *plan.backend, "/c", str(encoded_bitcode), "/clang:-Xclang", "/clang:-disable-llvm-passes", "/Fo" + str(object_path)]
    else:
        frontend = [plan.compiler, *plan.frontend, "-c", "-emit-llvm", "-o", str(input_bitcode)]
        backend = [plan.compiler, *plan.backend, "-c", "-x", "ir", str(encoded_bitcode), "-Xclang", "-disable-llvm-passes", "-o", str(object_path)]
    return frontend, backend


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-library", required=True, type=Path)
    parser.add_argument("--llvm-version", required=True)
    parser.add_argument("--policy-hash")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    options = parser.parse_args(argv)
    command = options.command[1:] if options.command[:1] == ["--"] else options.command
    try:
        version = tuple(int(part) for part in options.llvm_version.split("."))
        if len(version) != 3:
            raise ValueError("--llvm-version requires major.minor.patch")
        plan = compile_plan(command)
        if plan is None:
            return subprocess.run(command, check=False).returncode
        if not options.llvm_library.is_file():
            raise ValueError(f"LLVM library does not exist: {options.llvm_library}")
        with tempfile.TemporaryDirectory(prefix=".nwb_literals_", dir=plan.output.resolve().parent) as temporary:
            directory = Path(temporary)
            original = directory / "original.bc"
            encoded = directory / "encoded.bc"
            object_path = directory / ("output.obj" if plan.clang_cl else "output.o")
            frontend, backend = stage_commands(plan, original, encoded, object_path)
            result = run_tool(frontend, directory / "frontend.rsp")
            if result.returncode:
                return result.returncode
            transformer = importlib.import_module("launcher.string_literals.transformer")
            statistics = transformer.transform_bitcode(original, encoded, options.llvm_library, version)
            if not isinstance(statistics, dict) or not encoded.is_file():
                raise RuntimeError("Transformer did not produce a verified bitcode result")
            result = run_tool(backend, directory / "backend.rsp")
            if result.returncode:
                return result.returncode
            os.replace(object_path, plan.output)
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f"String-literal compiler: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    # Direct CMake invocation avoids executing the launcher's public CLI facade.
    import types
    package = types.ModuleType("launcher")
    package.__path__ = [str(Path(__file__).resolve().parents[1])]
    sys.modules.setdefault("launcher", package)
    raise SystemExit(main())

