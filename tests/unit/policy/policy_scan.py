#!/usr/bin/env python3
"""Shared helpers for first-party policy scanners."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Callable, Iterable, Sequence

from return_value_handling import blank_non_code, line_number


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
HEADER_SUFFIXES = frozenset((".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
PRODUCTION_DIRECTORIES = (
    "CoolStuff",
    "core",
    "global",
    "impl",
    "loader",
    "logger",
    "resource_cooker",
    "utilities",
)
FIRST_PARTY_DIRECTORIES = PRODUCTION_DIRECTORIES + ("tests",)
FIRST_PARTY_CODE_DIRECTORIES = (
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


Match = tuple[int, str]
SelfTestCase = tuple[str, str, tuple[Match, ...]]
Finder = Callable[[str], list[Match]]
FilesForRoot = Callable[[Path], list[Path]]


def files_under(
    source_root: Path,
    directories: Sequence[str],
    suffixes: frozenset[str],
) -> list[Path]:
    return sorted(
        path
        for relative_directory in directories
        for path in (source_root / relative_directory).rglob("*")
        if path.is_file() and path.suffix in suffixes
    )


def production_source_files(source_root: Path) -> list[Path]:
    return files_under(source_root, PRODUCTION_DIRECTORIES, SOURCE_SUFFIXES)


def first_party_source_files(source_root: Path) -> list[Path]:
    return files_under(source_root, FIRST_PARTY_DIRECTORIES, SOURCE_SUFFIXES)


def first_party_code_files(source_root: Path) -> list[Path]:
    return files_under(source_root, FIRST_PARTY_CODE_DIRECTORIES, SOURCE_SUFFIXES)


def header_files(source_root: Path) -> list[Path]:
    return files_under(source_root, FIRST_PARTY_CODE_DIRECTORIES, HEADER_SUFFIXES)


def find_regex_matches(source: str, pattern) -> list[Match]:
    code = blank_non_code(source)
    matches: list[Match] = []
    for match in pattern.finditer(code):
        if "identifier" in match.re.groupindex:
            ident = match.group("identifier")
        elif match.lastindex:
            ident = match.group(1)
        else:
            ident = match.group()
        matches.append((line_number(code, match.start()), ident))
    return matches


def run_self_test(finder: Finder, cases: Sequence[SelfTestCase]) -> int:
    failed = False
    for name, source, expected in cases:
        actual = tuple(finder(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def scan_paths(
    source_root: Path,
    paths: Iterable[Path],
    finder: Finder,
    violation_format: str,
) -> list[str]:
    violations: list[str] = []
    for path in paths:
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, identifier in finder(source):
            violations.append(
                violation_format.format(
                    path=path.relative_to(source_root),
                    line=line,
                    identifier=identifier,
                )
            )
    return violations


def run_policy(
    *,
    finder: Finder,
    files_for: FilesForRoot,
    error_header: str,
    violation_format: str,
    self_test_cases: Sequence[SelfTestCase],
) -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test(finder, self_test_cases)

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations = scan_paths(source_root, files_for(source_root), finder, violation_format)
    if violations:
        print(error_header, file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0
