#!/usr/bin/env python3
"""Keep the retired final-build test-feature escape hatch out of production code."""

from __future__ import annotations

import os
import sys
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
TEST_OVERRIDE = "NWB_ENABLE_TEST_FEATURE_OVERRIDES"
SKIPPED_DIRECTORIES = frozenset((
    ".cozter",
    ".git",
    "3rd_parties",
    "__build_obj",
    "__cmake",
    "tests",
))


def token_lines(source: str) -> tuple[int, ...]:
    return tuple(
        line_number
        for line_number, line in enumerate(source.splitlines(), start=1)
        if TEST_OVERRIDE in line
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
        for line_number in token_lines(source):
            violations.append(f"{path.relative_to(source_root)}:{line_number}: {TEST_OVERRIDE}")
    return violations


def run_self_test() -> int:
    cases = (
        ("empty", "", ()),
        ("one reference", f"#if defined({TEST_OVERRIDE})\n#endif", (1,)),
        ("comment reference", f"// {TEST_OVERRIDE}", (1,)),
        ("multiline", f"first\n{TEST_OVERRIDE}\nlast", (2,)),
    )
    failed = False
    for name, source, expected in cases:
        actual = token_lines(source)
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
        print("NWB_ENABLE_TEST_FEATURE_OVERRIDES is test-only and must not appear outside tests/.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
