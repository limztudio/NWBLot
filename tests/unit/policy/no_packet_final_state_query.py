#!/usr/bin/env python3
"""Keep recorded final-state lookup anchored to semantic graph tasks."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from return_value_handling import blank_non_code, line_number


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


SOURCE_DIRECTORIES = (
    "CoolStuff",
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
RETIRED_IDENTIFIER = re.compile(r"\bpacketFinalStateSeed\b")


def find_packet_final_state_queries(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    return [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_IDENTIFIER.finditer(code)
    ]


def source_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for relative_directory in SOURCE_DIRECTORIES
        for path in (source_root / relative_directory).rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def run_self_test() -> int:
    cases = (
        (
            "packet query call",
            "recordedGraph.packetFinalStateSeed(packet);",
            ((1, "packetFinalStateSeed"),),
        ),
        (
            "packet query declaration",
            "const State* packetFinalStateSeed(Packet packet)const;",
            ((1, "packetFinalStateSeed"),),
        ),
        (
            "inherited packet query",
            "using Base::packetFinalStateSeed;",
            ((1, "packetFinalStateSeed"),),
        ),
        (
            "semantic task query",
            "recordedGraph.taskFinalStateSeed(compiledGraph, task);",
            (),
        ),
        (
            "near names",
            "void packetFinalStateSeeds(); void packetFinalStateSeedForTask();",
            (),
        ),
        (
            "comments and literals",
            "// packetFinalStateSeed(packet);\n"
            'const char* text = "packetFinalStateSeed";\n'
            'const char* raw = R"tag(packetFinalStateSeed)tag";',
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_packet_final_state_queries(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for path in source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, identifier in find_packet_final_state_queries(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired packet final-state query '{identifier}'"
            )

    if violations:
        print(
            "Recorded final state must be resolved through semantic task IDs.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
