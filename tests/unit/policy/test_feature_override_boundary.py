#!/usr/bin/env python3
"""Keep retired mutable feature-support test seams out of production code."""

from __future__ import annotations

import os
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
FORBIDDEN_TOKENS = (
    "NWB_ENABLE_TEST_FEATURE_OVERRIDES",
    "setFeatureSupportDisabledForTesting",
    "clearFeatureSupportDisabledForTesting",
    "m_disabledFeatureSupportMask",
    "NWB_TESTBED_FORCE_RAYTRACING_EMULATION",
)
SKIPPED_DIRECTORIES = frozenset((
    ".cozter",
    ".git",
    ".idea",
    "3rd_parties",
    "__artifacts",
    "__build_obj",
    "__cmake",
    "__exec",
    "__pycache__",
    "cmake-build-debug",
    "tests",
))


def token_occurrences(source: str) -> tuple[tuple[int, str], ...]:
    return tuple(
        (line_number, token)
        for line_number, line in enumerate(source.splitlines(), start=1)
        for token in FORBIDDEN_TOKENS
        if token in line
    )


def production_files(source_root: Path) -> list[Path]:
    files: list[Path] = []
    for directory, child_directories, child_files in os.walk(source_root):
        child_directories[:] = [
            child_directory
            for child_directory in child_directories
            if child_directory not in SKIPPED_DIRECTORIES
        ]
        parent = Path(directory)
        for child_file in child_files:
            path = parent / child_file
            if path.is_file() and path.stat().st_size <= 1_000_000:
                files.append(path)
    return sorted(files)


def find_violations(source_root: Path) -> list[str]:
    violations: list[str] = []
    for path in production_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line_number, token in token_occurrences(source):
            violations.append(f"{path.relative_to(source_root)}:{line_number}: {token}")
    return violations


def run_self_test() -> int:
    cases = (
        ("empty", "", ()),
        (
            "one reference",
            f"#if defined({FORBIDDEN_TOKENS[0]})\n#endif",
            ((1, FORBIDDEN_TOKENS[0]),),
        ),
        (
            "comment reference",
            f"// {FORBIDDEN_TOKENS[1]}",
            ((1, FORBIDDEN_TOKENS[1]),),
        ),
        (
            "multiple references",
            f"{FORBIDDEN_TOKENS[2]}\nclean\n{FORBIDDEN_TOKENS[3]}",
            ((1, FORBIDDEN_TOKENS[2]), (3, FORBIDDEN_TOKENS[3])),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = token_occurrences(source)
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations = find_violations(source_root)
    if violations:
        print("Mutable GPU feature-support test seams must not appear outside tests/.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
