#!/usr/bin/env python3
"""Reject direct C++ standard-library references in project-owned test sources."""

from __future__ import annotations

from pathlib import Path
import re
import sys

from return_value_handling import blank_non_code


SOURCE_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
DIRECT_STD = re.compile(r"\bstd\s*::")


def line_number(source: str, position: int) -> int:
    return source.count("\n", 0, position) + 1


def find_direct_std_references(source: str) -> list[int]:
    code = blank_non_code(source)
    return [line_number(code, match.start()) for match in DIRECT_STD.finditer(code)]


def source_files(source_root: Path) -> list[Path]:
    return sorted(
        path
        for path in (source_root / "tests").rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def run_self_test() -> int:
    cases = (
        ("direct reference", "std::vector<int> values;", (1,)),
        ("spaced reference", "std  :: vector<int> values;", (1,)),
        ("comment", "// std::vector<int> values;", ()),
        ("literal", 'const char* text = "std::vector";', ()),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_direct_std_references(source))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else Path(__file__).resolve().parents[1]
    violations: list[str] = []
    for path in source_files(source_root):
        source = path.read_text(encoding="utf-8", errors="replace")
        for line in find_direct_std_references(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: direct std:: reference")

    if violations:
        print("Project-owned C++ test sources must use global features instead of direct std:: names:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
