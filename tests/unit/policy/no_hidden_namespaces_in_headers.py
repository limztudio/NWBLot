#!/usr/bin/env python3
"""Keep translation-unit-local symbols out of shared headers."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from return_value_handling import blank_non_code, line_number


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


SOURCE_DIRECTORIES = (
    "CoolStuff/Testbed",
    "core",
    "global",
    "impl",
    "loader",
    "logger",
    "resource_cooker",
    "tests",
    "utilities",
)
HEADER_SUFFIXES = frozenset((".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
HIDDEN_SYMBOL = re.compile(r"\b(__hidden_[A-Za-z0-9_]+)\b")


def find_hidden_symbols(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    return [
        (line_number(code, match.start()), match.group(1))
        for match in HIDDEN_SYMBOL.finditer(code)
    ]


def header_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for relative_directory in SOURCE_DIRECTORIES
        for path in (source_root / relative_directory).rglob("*")
        if path.is_file() and path.suffix in HEADER_SUFFIXES
    )


def run_self_test() -> int:
    cases = (
        ("hidden namespace", "namespace __hidden_parser{", ((1, "__hidden_parser"),)),
        ("hidden reference", "using Parser = __hidden_parser::Parser;", ((1, "__hidden_parser"),)),
        ("detail namespace", "namespace ParserDetail{", ()),
        ("comment", "// namespace __hidden_parser{", ()),
        ("literal", 'const char* text = "namespace __hidden_parser{";', ()),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_hidden_symbols(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for path in header_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, symbol in find_hidden_symbols(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: header references translation-unit-local symbol '{symbol}'")

    if violations:
        print("Use a named detail namespace for helpers shared through a header; reserve __hidden_* namespaces for one .cpp.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
