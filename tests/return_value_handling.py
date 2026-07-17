#!/usr/bin/env python3
"""Reject explicit casts that conceal first-party function-call return values."""

from __future__ import annotations

from pathlib import Path
import re
import sys


SOURCE_DIRECTORIES = (
    "Testbed",
    "core",
    "global",
    "impl",
    "loader",
    "logger",
    "resource_cooker",
    "tests",
    "utilities",
)
SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
STATIC_VOID_CAST = re.compile(r"static_cast\s*<\s*void\s*>\s*\(")
C_STYLE_VOID_CAST = re.compile(r"\(\s*void\s*\)")
CALL = re.compile(r"(?<!\w)([A-Za-z_]\w*)\s*\(")
CALL_AT_START = re.compile(
    r"(?:(?:::)?([A-Za-z_]\w*)(?:(?:::[A-Za-z_]\w*)|(?:->|\.)[A-Za-z_]\w*)*)\s*\("
)
FUNCTION_POINTER_CALL = re.compile(r"\(\s*\*\s*(?:[A-Za-z_]\w*\s*)?\)\s*\(")
NON_CALL_IDENTIFIERS = frozenset(("alignof", "decltype", "noexcept", "sizeof", "typeid"))


def blank_non_code(source: str) -> str:
    """Replace comments and literals with spaces while preserving line locations."""
    result = list(source)
    index = 0
    length = len(source)

    def blank(begin: int, end: int) -> None:
        for position in range(begin, end):
            if result[position] != "\n":
                result[position] = " "

    while index < length:
        if source.startswith("//", index):
            end = source.find("\n", index)
            if end == -1:
                end = length
            blank(index, end)
            index = end
            continue
        if source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = length if end == -1 else end + 2
            blank(index, end)
            index = end
            continue
        if source.startswith('R"', index):
            delimiter_end = source.find("(", index + 2)
            if delimiter_end != -1:
                delimiter = source[index + 2 : delimiter_end]
                terminator = ")" + delimiter + '"'
                end = source.find(terminator, delimiter_end + 1)
                end = length if end == -1 else end + len(terminator)
                blank(index, end)
                index = end
                continue
        if source[index] in ("'", '"'):
            quote = source[index]
            end = index + 1
            while end < length:
                if source[end] == "\\":
                    end += 2
                    continue
                if source[end] == quote:
                    end += 1
                    break
                end += 1
            blank(index, min(end, length))
            index = end
            continue
        index += 1

    return "".join(result)


def matching_parenthesis(source: str, opening: int) -> int | None:
    depth = 1
    for position in range(opening + 1, len(source)):
        if source[position] == "(":
            depth += 1
        elif source[position] == ")":
            depth -= 1
            if depth == 0:
                return position
    return None


def contains_call(expression: str) -> bool:
    if FUNCTION_POINTER_CALL.search(expression):
        return True
    return any(match.group(1) not in NON_CALL_IDENTIFIERS for match in CALL.finditer(expression))


def starts_with_call(expression: str) -> bool:
    start = len(expression) - len(expression.lstrip())
    call = CALL_AT_START.match(expression, start)
    if (call is not None and call.group(1) not in NON_CALL_IDENTIFIERS) or FUNCTION_POINTER_CALL.match(expression, start):
        return True
    if start >= len(expression) or expression[start] != "(":
        return False

    closing = matching_parenthesis(expression, start)
    if closing is None:
        return False
    if starts_with_call(expression[start + 1 : closing]):
        return True
    after_group = closing + 1
    while after_group < len(expression) and expression[after_group].isspace():
        after_group += 1
    return after_group < len(expression) and expression[after_group] == "("


def line_number(source: str, position: int) -> int:
    return source.count("\n", 0, position) + 1


def find_discarded_calls(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    violations: list[tuple[int, str]] = []

    for match in STATIC_VOID_CAST.finditer(code):
        opening = match.end() - 1
        closing = matching_parenthesis(code, opening)
        if closing is not None and contains_call(code[opening + 1 : closing]):
            violations.append((line_number(code, match.start()), "static_cast<void>"))

    for match in C_STYLE_VOID_CAST.finditer(code):
        expression = code[match.end() :]
        if starts_with_call(expression):
            violations.append((line_number(code, match.start()), "C-style void cast"))

    return violations


def source_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for directory in SOURCE_DIRECTORIES
        for path in (source_root / directory).rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def main() -> int:
    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else Path(__file__).resolve().parents[1]
    violations: list[str] = []
    for path in source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, cast_kind in find_discarded_calls(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: {cast_kind} conceals a function-call return value")

    if violations:
        print("Explicit void casts must not discard function-call return values:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
