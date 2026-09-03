#!/usr/bin/env python3
"""Reject direct project/implementation C++ dependencies from core sources."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from policy_scan import REPOSITORY_ROOT, SOURCE_SUFFIXES, blank_non_code, line_number
INCLUDE_DIRECTIVE = re.compile(r"^\s*#\s*include\b", re.MULTILINE)
FORBIDDEN_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]\s*(?:impl|CoolStuff|tests|utilities)(?:/|[">])')
IMPLEMENTATION_TARGET_DECLARATION = re.compile(r"\bnwb_declare_(?:static|interface)_library\s*\(\s*(nwb_[A-Za-z0-9_]+)")


def source_line(source: str, position: int) -> str:
    begin = source.rfind("\n", 0, position) + 1
    end = source.find("\n", position)
    return source[begin:] if end == -1 else source[begin:end]


def blank_cmake_comments(source: str) -> str:
    return re.sub(r"#[^\r\n]*", "", source)


def find_forbidden_includes(source: str) -> list[int]:
    code = blank_non_code(source)
    violations: list[int] = []
    for match in INCLUDE_DIRECTIVE.finditer(code):
        if FORBIDDEN_INCLUDE.match(source_line(source, match.start())):
            violations.append(line_number(source, match.start()))
    return violations


def implementation_targets(source_root: Path) -> frozenset[str]:
    targets: set[str] = set()
    for path in (source_root / "impl").rglob("CMakeLists.txt"):
        source = blank_cmake_comments(path.read_text(encoding="utf-8", errors="replace"))
        targets.update(match.group(1) for match in IMPLEMENTATION_TARGET_DECLARATION.finditer(source))
    return frozenset(targets)


def find_forbidden_target_references(source: str, targets: frozenset[str]) -> list[str]:
    code = blank_cmake_comments(source)
    return [target for target in sorted(targets) if re.search(rf"\b{re.escape(target)}\b", code)]


def source_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for path in (source_root / "core").rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def cmake_files(source_root: Path) -> list[Path]:
    return sorted((source_root / "core").rglob("CMakeLists.txt"))


def run_self_test() -> int:
    cases = (
        ("implementation angle include", "#include <impl/ecs_scene/components.h>", (1,)),
        ("project quoted include", '# include "CoolStuff/Testbed/runtime.h"', (1,)),
        ("test angle include", "#include <tests/common/test_context.h>", (1,)),
        ("core include", "#include <core/graphics/api.h>", ()),
        ("global include", '#include "global/global.h"', ()),
        ("line comment", "// #include <impl/ecs_scene/components.h>", ()),
        ("block comment", "/*\n#include <impl/ecs_scene/components.h>\n*/", ()),
        ("literal", 'const char* source = "#include <impl/ecs_scene/components.h>";', ()),
    )
    target_cases = (
        ("implementation target", "target_link_libraries(nwb_core PRIVATE nwb_ecs_scene)", ("nwb_ecs_scene",)),
        ("comment", "# target_link_libraries(nwb_core PRIVATE nwb_ecs_scene)", ()),
        ("core target", "target_link_libraries(nwb_core PRIVATE nwb_graphics)", ()),
    )
    targets = frozenset(("nwb_ecs_scene", "nwb_assets_material"))
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_forbidden_includes(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    for name, source, expected in target_cases:
        actual = tuple(find_forbidden_target_references(source, targets))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    targets = implementation_targets(source_root)
    if not targets:
        print("Failed to discover implementation CMake targets.", file=sys.stderr)
        return 1

    violations: list[str] = []
    for path in source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line in find_forbidden_includes(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: direct project/implementation include")
    for path in cmake_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for target in find_forbidden_target_references(source, targets):
            violations.append(f"{path.relative_to(source_root)}: direct implementation target reference '{target}'")

    if violations:
        print("Core source must not directly include or link implementation/project code.", file=sys.stderr)
        print("Project-owned material and shader assets remain outside this source-level dependency policy.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
