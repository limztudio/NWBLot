#!/usr/bin/env python3
"""Place the literal decoder runtime before the image's other link inputs."""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path
from typing import Optional, Sequence

if __package__:
    from .compiler import driver_response_quoting, expand_response_files, run_tool
else:
    from compiler import driver_response_quoting, expand_response_files, run_tool


LINK_VALUE_OPTIONS = {
    "-o", "-L", "-F", "-l", "-arch", "-target", "--target", "-isysroot", "--sysroot",
    "-syslibroot", "-u", "-e", "-T", "-Xlinker", "-framework", "-weak_framework",
    "-rpath", "-install_name", "-compatibility_version", "-current_version",
}
LINK_INPUT_SUFFIXES = {".o", ".obj", ".a", ".lib", ".so", ".dylib", ".bc", ".lo"}


def order_runtime_object(arguments: Sequence[str], runtime_object: Path, quoting: Optional[str] = None) -> list[str]:
    expanded = expand_response_files(arguments, quoting)
    expected = os.path.normcase(str(runtime_object.resolve()))
    matches = []
    inputs = []
    index = 0
    while index < len(expanded):
        argument = expanded[index]
        if argument in LINK_VALUE_OPTIONS:
            if index + 1 == len(expanded):
                raise ValueError(f"Missing link value for {argument}")
            if argument in {"-l", "-framework", "-weak_framework"}:
                inputs.append(index)
            index += 2
            continue
        if not argument.startswith("-") and os.path.normcase(str(Path(argument).resolve())) == expected:
            matches.append(index)
            inputs.append(index)
        elif argument.startswith("-l") or (not argument.startswith("-") and not argument.upper().startswith(("/OUT:", "/PDB:", "/IMPLIB:", "/LIBPATH:", "/DEF:")) and Path(argument).suffix.lower() in LINK_INPUT_SUFFIXES):
            inputs.append(index)
        index += 1
    if len(matches) != 1:
        raise ValueError(f"Link command must contain the exact runtime object once; found {len(matches)}: {runtime_object}")
    position = matches[0]
    first = min(inputs)
    if position != first:
        runtime_argument = expanded.pop(position)
        expanded.insert(first, runtime_argument)
    return expanded


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-object", required=True, type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    options = parser.parse_args(argv)
    command = options.command[1:] if options.command[:1] == ["--"] else options.command
    try:
        if not command:
            raise ValueError("Missing link command after --")
        if not options.runtime_object.is_file():
            raise ValueError(f"Runtime object does not exist: {options.runtime_object}")
        wrapped = Path(command[0]).stem.lower() == "cmake"
        tool = command[command.index("--") + 1] if wrapped else command[0]
        arguments = order_runtime_object(command[1:], options.runtime_object, driver_response_quoting(tool, command[1:]))
        with tempfile.TemporaryDirectory(prefix="nwb_literal_link_") as temporary:
            if wrapped:
                separator = arguments.index("--")
                result = run_tool(arguments[separator + 1:], Path(temporary) / "link.rsp", [command[0], *arguments[:separator + 1]])
                return result.returncode
            return run_tool([command[0], *arguments], Path(temporary) / "link.rsp").returncode
    except (OSError, ValueError) as error:
        print(f"String-literal linker: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

