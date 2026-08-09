#!/usr/bin/env python3
"""Reject direct standard-library containers in designated interop boundaries."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from return_value_handling import blank_non_code, line_number


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]


INTEROP_BOUNDARIES = (
    "loader/main.cpp",
    "logger/server/crash_symbolicate_rgd.cpp",
    "resource_cooker/command_line.cpp",
    "utilities/fbx_to_nwb/command_line.cpp",
    "utilities/tex_conv/command_line.cpp",
)
DIRECT_INTEROP_CONTAINER = re.compile(r"\bstd\s*::\s*(?:string|vector)\b")


def find_direct_interop_containers(source: str) -> list[int]:
    code = blank_non_code(source)
    return [line_number(code, match.start()) for match in DIRECT_INTEROP_CONTAINER.finditer(code)]


def run_self_test() -> int:
    cases = (
        ("direct string", "std::string value;", (1,)),
        ("direct vector", "std :: vector<int> values;", (1,)),
        ("comment", "// std::string value;", ()),
        ("literal", 'const char* text = "std::vector";', ()),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_direct_interop_containers(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for relative_path in INTEROP_BOUNDARIES:
        path = source_root / relative_path
        source = path.read_text(encoding="utf-8", errors="replace")
        for line in find_direct_interop_containers(source):
            violations.append(f"{relative_path}:{line}: direct std::string/std::vector reference")

    if violations:
        print("Interop boundaries must use project-owned aliases instead of direct std::string/std::vector names:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
