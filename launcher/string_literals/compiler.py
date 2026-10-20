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


MAIN_ENTRY = "__main__"
ROOT_PACKAGE = "launcher"
COMMAND_ARGUMENT = "command"
RESPONSE_FILE_ENCODING = "utf-8"
QUOTING_WINDOWS = "windows"
QUOTING_POSIX = "posix"
QUOTING_MODES = {QUOTING_WINDOWS, QUOTING_POSIX}
RESPONSE_QUOTING_OPTION = "--rsp-quoting="
CLANG_CL_PASSTHROUGH_PREFIX = "/clang:"
CLANG_CL_DRIVER_NAME = "clang-cl"
CL_DRIVER_MODE_OPTION = "--driver-mode=cl"
MAX_RESPONSE_FILE_DEPTH = 64
DOUBLED_QUOTE_WIDTH = 2
OPTION_WITH_VALUE_WIDTH = 2
CLANG_CL_SOURCE_OPTION_LENGTH = 3
XCLANG_PAIR_ARGUMENT_COUNT = 4
XCLANG_PAIR_SECOND_MARKER_OFFSET = 2
XCLANG_PAIR_LAST_OFFSET = 3
LLVM_VERSION_PART_COUNT = 3
UNTERMINATED_QUOTE_MESSAGE = "Unterminated quote in response file"
SINGLE_OUTPUT_MESSAGE = "Object compilation requires exactly one output path"
OUTPUT_OPTION = "-o"
CLANG_CL_OUTPUT_OPTION = "/Fo"
COMPILE_OPTION = "-c"
CLANG_CL_COMPILE_OPTION = "/c"
EMIT_LLVM_OPTION = "-emit-llvm"
XCLANG_OPTION = "-Xclang"
LANGUAGE_OPTION = "-x"
DEPENDENCY_FILE_OPTION = "-MF"
DEPENDENCY_TARGET_OPTION = "-MT"
DEPENDENCY_QUOTED_TARGET_OPTION = "-MQ"
CLANG_CL_PRECOMPILED_CREATE_OPTION = "/Yc"
TRANSFORMED_LANGUAGES = {"c", "c++"}
SOURCE_LANGUAGES = {"c", "c++", "objective-c", "objective-c++"}
CLANG_CL_FRONTEND_PREFIX_OPTIONS = ("/D", "/U", "/I", "/FI", "/Yu", CLANG_CL_PRECOMPILED_CREATE_OPTION, "/Fp", "/std:", "/showIncludes", "/sourceDependencies", "/W", "/external:")
CLANG_CL_FRONTEND_SINGLE_OPTIONS = {"/TC", "/TP", "/WX", "/WX-", "/w"}
CLANG_CL_VALUE_OPTIONS = {"/D", "/U", "/I", "/FI", "/Yu", "/Fp", "/external:I", "/sourceDependencies"}

SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".c++", ".C"}
FRONTEND_VALUE_OPTIONS = { "-D", "-U", "-I", "-isystem", "-iquote", "-idirafter", "-include", "-imacros", "-include-pch", "-iprefix", "-iwithprefix", "-iwithprefixbefore", "-isystem-after", "-iframework", "-iframeworkwithsysroot", "-ivfsoverlay",
    DEPENDENCY_FILE_OPTION, DEPENDENCY_TARGET_OPTION, DEPENDENCY_QUOTED_TARGET_OPTION, "-MJ", "-dependency-file", "-serialize-diagnostics", LANGUAGE_OPTION, "-std", }
DRIVER_VALUE_OPTIONS = { "-target", "--target", "-arch", "-isysroot", "--sysroot", "-resource-dir", "-B", "-gcc-toolchain", "--gcc-toolchain", "-Xassembler", "-mllvm", "-Xpreprocessor", XCLANG_OPTION, "-fdebug-compilation-dir", "-fcoverage-compilation-dir", }
FRONTEND_SINGLE_OPTIONS = { "-MD", "-MMD", "-MP", "-MG", "-fno-rtti", "-frtti", "-fno-exceptions", "-fexceptions", "-nostdinc", "-nostdinc++", "-undef", "-pedantic", "-pedantic-errors", }
FRONTEND_PREFIX_OPTIONS = ("-D", "-U", "-I", "-std=", DEPENDENCY_FILE_OPTION, DEPENDENCY_TARGET_OPTION, DEPENDENCY_QUOTED_TARGET_OPTION, "-MJ", "-fdiagnostics-", "-isystem", "-iquote", "-idirafter", "-include", "-imacros", "-iframework", "-fmacro-prefix-map=", "-fmodules-cache-path=", "-fmodule-file=",)


def effective_argument(argument: str) -> str:
    return argument[len(CLANG_CL_PASSTHROUGH_PREFIX):] if argument.startswith(CLANG_CL_PASSTHROUGH_PREFIX) else argument


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
                    raise ValueError(UNTERMINATED_QUOTE_MESSAGE)
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
                    index += DOUBLED_QUOTE_WIDTH
                    continue
                quoted = not quoted
            else:
                word.append(text[index])
            index += 1
        if quoted:
            raise ValueError(UNTERMINATED_QUOTE_MESSAGE)
        result.append("".join(word))
    return result


def driver_response_quoting(tool: str, arguments: Sequence[str] = ()) -> str:
    name = Path(tool).stem.lower()
    return QUOTING_WINDOWS if name.endswith((CLANG_CL_DRIVER_NAME, "lld-link")) or name == "link" or CL_DRIVER_MODE_OPTION in arguments else QUOTING_POSIX


def expand_response_files(arguments: Sequence[str], quoting: Optional[str] = None) -> list[str]:
    mode = quoting or QUOTING_POSIX
    for argument in arguments:
        if argument.startswith(RESPONSE_QUOTING_OPTION):
            mode = argument.split("=", 1)[1]
    if mode not in QUOTING_MODES:
        raise ValueError(f"Unsupported response quoting: {mode}")

    def expand(items: Sequence[str], ancestors: tuple[Path, ...]) -> list[str]:
        expanded = []
        for argument in items:
            if not argument.startswith("@"):
                expanded.append(argument)
                continue
            path = Path(argument[1:]).resolve()
            if path in ancestors or len(ancestors) >= MAX_RESPONSE_FILE_DEPTH:
                raise ValueError(f"Recursive response file: {path}")
            data = path.read_bytes()
            encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
            text = data.decode(encoding)
            tokens = split_windows_response(text) if mode == QUOTING_WINDOWS else split_gnu_response(text)
            expanded.extend(expand(tokens, ancestors + (path,)))
        return expanded

    return expand(arguments, ())


def run_tool(command: Sequence[str], response_path: Path, prefix: Sequence[str] = ()) -> subprocess.CompletedProcess:
    """Keep expanded compiler/linker arguments below host command-line limits."""
    mode = driver_response_quoting(command[0], command[1:])
    controls = [argument for argument in command[1:] if argument.startswith(RESPONSE_QUOTING_OPTION)]
    if controls:
        mode = controls[-1].split("=", 1)[1]
    if mode not in QUOTING_MODES:
        raise ValueError(f"Unsupported response quoting: {mode}")
    arguments = [argument for argument in command[1:] if not argument.startswith(RESPONSE_QUOTING_OPTION)]
    text = subprocess.list2cmdline(arguments) if mode == QUOTING_WINDOWS else quote_gnu_response(arguments)
    response_path.write_text(text + "\n", encoding=RESPONSE_FILE_ENCODING)
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
    clang_cl = Path(compiler).stem.lower().endswith(CLANG_CL_DRIVER_NAME) or CL_DRIVER_MODE_OPTION in arguments
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
        if argument in {OUTPUT_OPTION, CLANG_CL_OUTPUT_OPTION}:
            if index + 1 == len(arguments) or output is not None:
                raise ValueError(SINGLE_OUTPUT_MESSAGE)
            output = Path(effective_argument(arguments[index + 1]))
            index += OPTION_WITH_VALUE_WIDTH
            continue
        if (argument.startswith(OUTPUT_OPTION) and len(argument) > len(OUTPUT_OPTION)) or (clang_cl and argument.startswith(CLANG_CL_OUTPUT_OPTION) and len(argument) > len(CLANG_CL_OUTPUT_OPTION)):
            if output is not None:
                raise ValueError(SINGLE_OUTPUT_MESSAGE)
            output = Path(argument[len(CLANG_CL_OUTPUT_OPTION):] if argument.startswith(CLANG_CL_OUTPUT_OPTION) else argument[len(OUTPUT_OPTION):])
            index += 1
            continue
        if argument in {COMPILE_OPTION, CLANG_CL_COMPILE_OPTION}:
            object_compile = True
            index += 1
            continue
        if argument in {"-E", "-M", "-MM", "-fsyntax-only", "--version", "-dumpversion", "-dumpmachine", "-###"} or (clang_cl and argument in {"/E", "/EP", "/P", "/?"}):
            passthrough = True
        if argument in {"-S", EMIT_LLVM_OPTION, "-save-temps", "-save-temps=obj"} or argument.startswith(("-flto", "-fmodule-output", "-fmodules-ts")):
            unsupported = argument
        if argument in FRONTEND_VALUE_OPTIONS or argument in DRIVER_VALUE_OPTIONS:
            if index + 1 == len(arguments):
                raise ValueError(f"Missing value for {argument}")
            value = effective_argument(arguments[index + 1])
            pair = arguments[index:index + OPTION_WITH_VALUE_WIDTH]
            frontend.extend(pair)
            if argument == LANGUAGE_OPTION:
                language = value
            elif argument == DEPENDENCY_FILE_OPTION:
                dependency_path = True
            elif argument in {DEPENDENCY_TARGET_OPTION, DEPENDENCY_QUOTED_TARGET_OPTION}:
                dependency_target = True
            elif argument == XCLANG_OPTION and value in {"-emit-pch", "-emit-pth"}:
                passthrough = True
            if argument == XCLANG_OPTION and value in {"-include-pch", "-include", "-imacros"}:
                if index + XCLANG_PAIR_LAST_OFFSET >= len(arguments) or effective_argument(arguments[index + XCLANG_PAIR_SECOND_MARKER_OFFSET]) != XCLANG_OPTION:
                    raise ValueError(f"Unsupported paired frontend option: {value}")
                frontend.extend(arguments[index + XCLANG_PAIR_SECOND_MARKER_OFFSET:index + XCLANG_PAIR_ARGUMENT_COUNT])
                index += XCLANG_PAIR_ARGUMENT_COUNT
                continue
            if argument not in FRONTEND_VALUE_OPTIONS:
                backend.extend(pair)
            index += OPTION_WITH_VALUE_WIDTH
            continue
        if clang_cl and argument.startswith(("/Tc", "/Tp")):
            if len(argument) == CLANG_CL_SOURCE_OPTION_LENGTH:
                if index + 1 == len(arguments):
                    raise ValueError(f"Missing source for {argument}")
                frontend.extend(arguments[index:index + OPTION_WITH_VALUE_WIDTH])
                source_indices.add(index + 1)
                index += OPTION_WITH_VALUE_WIDTH
            else:
                frontend.append(raw)
                source_indices.add(index)
                index += 1
            continue
        is_source = Path(argument).suffix in SOURCE_SUFFIXES and not argument.startswith("-")
        if not is_source and language in SOURCE_LANGUAGES:
            is_source = not argument.startswith(("-", "/")) or Path(argument).is_file()
        if is_source:
            source_indices.add(index)
            frontend.append(raw)
            index += 1
            continue
        frontend.append(raw)
        if argument in {"-MD", "-MMD"}:
            dependency = True
        if argument.startswith(DEPENDENCY_FILE_OPTION):
            dependency_path = True
        if argument.startswith((DEPENDENCY_TARGET_OPTION, DEPENDENCY_QUOTED_TARGET_OPTION)):
            dependency_target = True
        front_only = argument in FRONTEND_SINGLE_OPTIONS or argument.startswith(FRONTEND_PREFIX_OPTIONS)
        front_only = front_only or argument.startswith("-W") or argument == "-w"
        if clang_cl:
            front_only = front_only or argument.startswith(CLANG_CL_FRONTEND_PREFIX_OPTIONS) or argument in CLANG_CL_FRONTEND_SINGLE_OPTIONS
            if argument.startswith(CLANG_CL_PRECOMPILED_CREATE_OPTION):
                passthrough = True
            if argument in CLANG_CL_VALUE_OPTIONS or argument.startswith("/sourceDependencies:"):
                if index + 1 == len(arguments):
                    raise ValueError(f"Missing value for {argument}")
                frontend.append(arguments[index + 1])
                index += OPTION_WITH_VALUE_WIDTH
                continue
        if not front_only:
            backend.append(raw)
        index += 1
    if passthrough or (language is not None and language not in TRANSFORMED_LANGUAGES):
        return None
    if not source_indices:
        if object_compile and language in TRANSFORMED_LANGUAGES:
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
        prefix = CLANG_CL_PASSTHROUGH_PREFIX if clang_cl else ""
        if not dependency_path:
            frontend.extend((prefix + DEPENDENCY_FILE_OPTION, prefix + str(output.with_suffix(".d"))))
        if not dependency_target:
            frontend.extend((prefix + DEPENDENCY_TARGET_OPTION, prefix + str(output)))
    return CompilePlan(compiler, tuple(frontend), tuple(backend), output, clang_cl)


def stage_commands(plan: CompilePlan, input_bitcode: Path, encoded_bitcode: Path, object_path: Path) -> tuple[list[str], list[str]]:
    if plan.clang_cl:
        frontend = [plan.compiler, *plan.frontend, CLANG_CL_COMPILE_OPTION, "/clang:-emit-llvm", CLANG_CL_OUTPUT_OPTION + str(input_bitcode)]
        backend = [plan.compiler, *plan.backend, CLANG_CL_COMPILE_OPTION, str(encoded_bitcode), "/clang:-Xclang", "/clang:-disable-llvm-passes", CLANG_CL_OUTPUT_OPTION + str(object_path)]
    else:
        frontend = [plan.compiler, *plan.frontend, COMPILE_OPTION, EMIT_LLVM_OPTION, OUTPUT_OPTION, str(input_bitcode)]
        backend = [plan.compiler, *plan.backend, COMPILE_OPTION, LANGUAGE_OPTION, "ir", str(encoded_bitcode), XCLANG_OPTION, "-disable-llvm-passes", OUTPUT_OPTION, str(object_path)]
    return frontend, backend


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-library", required=True, type=Path)
    parser.add_argument("--llvm-version", required=True)
    parser.add_argument("--policy-hash")
    parser.add_argument(COMMAND_ARGUMENT, nargs=argparse.REMAINDER)
    options = parser.parse_args(argv)
    command = options.command[1:] if options.command[:1] == ["--"] else options.command
    try:
        version = tuple(int(part) for part in options.llvm_version.split("."))
        if len(version) != LLVM_VERSION_PART_COUNT:
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


if __name__ == MAIN_ENTRY:
    # Direct CMake invocation avoids executing the launcher's public CLI facade.
    import types
    package = types.ModuleType(ROOT_PACKAGE)
    package.__path__ = [str(Path(__file__).resolve().parents[1])]
    sys.modules.setdefault(ROOT_PACKAGE, package)
    raise SystemExit(main())

