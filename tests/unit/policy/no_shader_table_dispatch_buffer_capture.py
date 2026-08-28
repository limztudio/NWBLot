#!/usr/bin/env python3
"""Keep the retired ShaderTable dispatch-buffer capture seam out of production C++ sources."""

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
RETIRED_IDENTIFIER = re.compile(r"\bcaptureDispatchBuffersForTesting\b")


def find_shader_table_dispatch_buffer_capture_references(source: str) -> list[tuple[int, str]]:
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
        (
            "capture call",
            "table.captureDispatchBuffersForTesting(buffers);",
            ((1, "captureDispatchBuffersForTesting"),),
        ),
        (
            "capture declaration",
            "void captureDispatchBuffersForTesting(Array<BufferHandle, 4u>& outBuffers)const;",
            ((1, "captureDispatchBuffersForTesting"),),
        ),
        (
            "multiple captures",
            "table.captureDispatchBuffersForTesting(oldBuffers);\n"
            "table.captureDispatchBuffersForTesting(newBuffers);",
            (
                (1, "captureDispatchBuffersForTesting"),
                (2, "captureDispatchBuffersForTesting"),
            ),
        ),
        (
            "comments",
            "// table.captureDispatchBuffersForTesting(buffers);\n"
            "/* void captureDispatchBuffersForTesting(); */",
            (),
        ),
        (
            "literals",
            'const char* text = "captureDispatchBuffersForTesting";\n'
            'const char* raw = R"tag(captureDispatchBuffersForTesting)tag";',
            (),
        ),
        (
            "remaining rejection hooks",
            "table.rejectNextBufferAllocationForTesting();\n"
            "table.rejectNextNewBufferMapForTesting();",
            (),
        ),
        (
            "near names",
            "void captureDispatchBuffersForTestingAgain();\n"
            "void captureDispatchBufferForTesting();",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_shader_table_dispatch_buffer_capture_references(source))
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
        for line, identifier in find_shader_table_dispatch_buffer_capture_references(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired ShaderTable dispatch-buffer capture '{identifier}'"
            )

    if violations:
        print("Production ShaderTable code must not expose dispatch-buffer capture for tests.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
