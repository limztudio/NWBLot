#!/usr/bin/env python3
"""Keep retired staging-texture test seams out of production C++ sources."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from return_value_handling import blank_non_code, line_number


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


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
SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
RETIRED_IDENTIFIER = re.compile(
    r"\b(?:hasMappedMemoryForTesting|isPersistentlyMappedForTesting|"
    r"rejectNextInvalidateForTesting|m_rejectNextInvalidateForTesting)\b"
)


def find_staging_texture_test_facade_references(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    return [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_IDENTIFIER.finditer(code)
    ]


def production_source_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for relative_directory in PRODUCTION_DIRECTORIES
        for path in (source_root / relative_directory).rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def run_self_test() -> int:
    cases = (
        ("mapped-memory query", "staging.hasMappedMemoryForTesting();", ((1, "hasMappedMemoryForTesting"),)),
        (
            "persistent-mapping query",
            "staging.isPersistentlyMappedForTesting();",
            ((1, "isPersistentlyMappedForTesting"),),
        ),
        (
            "invalidate rejection command",
            "staging.rejectNextInvalidateForTesting();",
            ((1, "rejectNextInvalidateForTesting"),),
        ),
        (
            "invalidate rejection state",
            "bool m_rejectNextInvalidateForTesting = false;",
            ((1, "m_rejectNextInvalidateForTesting"),),
        ),
        (
            "multiple identifiers",
            "if(staging.hasMappedMemoryForTesting())\n"
            "    staging.rejectNextInvalidateForTesting();",
            (
                (1, "hasMappedMemoryForTesting"),
                (2, "rejectNextInvalidateForTesting"),
            ),
        ),
        (
            "comments",
            "// staging.hasMappedMemoryForTesting();\n"
            "/* staging.rejectNextInvalidateForTesting(); */",
            (),
        ),
        (
            "literals",
            'const char* text = "isPersistentlyMappedForTesting m_rejectNextInvalidateForTesting";\n'
            'const char* raw = R"tag(rejectNextInvalidateForTesting)tag";',
            (),
        ),
        (
            "near names",
            "bool hasMappedMemoryForTestingAgain();\n"
            "bool isPersistentlyMappedForTestings();\n"
            "void rejectNextInvalidateForTestingLater();\n"
            "bool m_rejectNextInvalidateForTestingState = false;",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_staging_texture_test_facade_references(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for path in production_source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, identifier in find_staging_texture_test_facade_references(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired staging-texture test facade '{identifier}'"
            )

    if violations:
        print("Production staging-texture behavior must not depend on retired test-only mapping controls.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
