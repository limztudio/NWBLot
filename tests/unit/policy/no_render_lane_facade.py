#!/usr/bin/env python3
"""Keep the retired RenderLane facade out of production C++ sources."""

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
RETIRED_FACADE_IDENTIFIER = re.compile(r"\b(?:RenderLane|renderLane|setRenderLane|resolveRenderLane|isRenderLaneDedicated)\b")


def find_render_lane_references(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    return [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_FACADE_IDENTIFIER.finditer(code)
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
        ("namespace", "namespace RenderLane{ enum Enum{}; };", ((1, "RenderLane"),)),
        ("field", "CommandQueue::Enum renderLane = CommandQueue::Graphics;", ((1, "renderLane"),)),
        ("setter", "parameters.setRenderLane(CommandQueue::Compute);", ((1, "setRenderLane"),)),
        ("resolver", "device.resolveRenderLane(lane);", ((1, "resolveRenderLane"),)),
        ("dedicated query", "device.isRenderLaneDedicated(lane);", ((1, "isRenderLaneDedicated"),)),
        ("comment", "// RenderLane::Graphics", ()),
        ("block comment", "/* setRenderLane(RenderLane::AsyncCompute) */", ()),
        ("literal", 'const char* text = "resolveRenderLane";', ()),
        (
            "similarly named identifiers",
            "struct RenderLanePolicy{ int renderLaneIndex; void setRenderLanePolicy(); bool isRenderLaneDedicatedQueue(); };",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_render_lane_references(source))
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
        for line, identifier in find_render_lane_references(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: retired RenderLane facade identifier '{identifier}'")

    if violations:
        print("Production C++ must use physical CommandQueue identities instead of the retired RenderLane facade.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
