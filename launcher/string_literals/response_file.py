"""Parse response files and run tools with driver-specific quoting."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Optional, Sequence

RESPONSE_FILE_ENCODING = "utf-8"
QUOTING_WINDOWS = "windows"
QUOTING_POSIX = "posix"
QUOTING_MODES = {QUOTING_WINDOWS, QUOTING_POSIX}
RESPONSE_QUOTING_OPTION = "--rsp-quoting="
CLANG_CL_DRIVER_NAME = "clang-cl"
CL_DRIVER_MODE_OPTION = "--driver-mode=cl"
MAX_RESPONSE_FILE_DEPTH = 64
DOUBLED_QUOTE_WIDTH = 2
ESCAPED_BACKSLASH_PAIR_WIDTH = 2
UNTERMINATED_QUOTE_MESSAGE = "Unterminated quote in response file"


class ResponseFileTool:
    @staticmethod
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


    @staticmethod
    def quote_gnu_response(arguments: Sequence[str]) -> str:
        return " ".join('"' + argument.replace("\\", "\\\\").replace('"', '\\"') + '"' for argument in arguments)


    @staticmethod
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
                        word.append("\\" * (count // ESCAPED_BACKSLASH_PAIR_WIDTH))
                        if count % ESCAPED_BACKSLASH_PAIR_WIDTH:
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


    @staticmethod
    def driver_response_quoting(tool: str, arguments: Sequence[str] = ()) -> str:
        name = Path(tool).stem.lower()
        return QUOTING_WINDOWS if name.endswith((CLANG_CL_DRIVER_NAME, "lld-link")) or name == "link" or CL_DRIVER_MODE_OPTION in arguments else QUOTING_POSIX


    @staticmethod
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
                tokens = ResponseFileTool.split_windows_response(text) if mode == QUOTING_WINDOWS else ResponseFileTool.split_gnu_response(text)
                expanded.extend(expand(tokens, ancestors + (path,)))
            return expanded

        return expand(arguments, ())


    @staticmethod
    def run_tool(command: Sequence[str], response_path: Path, prefix: Sequence[str] = ()) -> subprocess.CompletedProcess:
        """Keep expanded compiler/linker arguments below host command-line limits."""
        mode = ResponseFileTool.driver_response_quoting(command[0], command[1:])
        controls = [argument for argument in command[1:] if argument.startswith(RESPONSE_QUOTING_OPTION)]
        if controls:
            mode = controls[-1].split("=", 1)[1]
        if mode not in QUOTING_MODES:
            raise ValueError(f"Unsupported response quoting: {mode}")
        arguments = [argument for argument in command[1:] if not argument.startswith(RESPONSE_QUOTING_OPTION)]
        text = subprocess.list2cmdline(arguments) if mode == QUOTING_WINDOWS else ResponseFileTool.quote_gnu_response(arguments)
        response_path.write_text(text + "\n", encoding=RESPONSE_FILE_ENCODING)
        return subprocess.run([*prefix, command[0], *controls, "@" + str(response_path.resolve())], check=False)

