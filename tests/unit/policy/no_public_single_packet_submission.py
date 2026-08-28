#!/usr/bin/env python3
"""Keep native single-packet submission private to the graph runtime."""

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
SUBMITTER_CLASS_OPEN = re.compile(r"\b(class|struct)\s+GpuTaskGraphSubmitter\b[^;{]*\{")
SUBMITTER_MEMBER_TOKEN = re.compile(
    r"\b(?:(public|private|protected)\s*:|(submitPacket)\b)"
)


def submitter_body_ranges(code: str) -> list[tuple[int, int, str]]:
    ranges: list[tuple[int, int, str]] = []
    for match in SUBMITTER_CLASS_OPEN.finditer(code):
        open_offset = match.end() - 1
        default_access = "public" if match.group(1) == "struct" else "private"
        depth = 0
        for offset in range(open_offset, len(code)):
            if code[offset] == "{":
                depth += 1
            elif code[offset] == "}":
                depth -= 1
                if depth == 0:
                    ranges.append((open_offset + 1, offset, default_access))
                    break
    return ranges


def is_class_body_top_level(code: str, start: int, offset: int) -> bool:
    brace_depth = 0
    parenthesis_depth = 0
    bracket_depth = 0
    for character in code[start:offset]:
        if character == "{":
            brace_depth += 1
        elif character == "}":
            brace_depth -= 1
        elif character == "(":
            parenthesis_depth += 1
        elif character == ")":
            parenthesis_depth -= 1
        elif character == "[":
            bracket_depth += 1
        elif character == "]":
            bracket_depth -= 1
    return brace_depth == 0 and parenthesis_depth == 0 and bracket_depth == 0


def find_public_single_packet_submission(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references: list[tuple[int, str]] = []
    for start, end, default_access in submitter_body_ranges(code):
        access = default_access
        for match in SUBMITTER_MEMBER_TOKEN.finditer(code, start, end):
            if not is_class_body_top_level(code, start, match.start()):
                continue
            if match.group(1):
                access = match.group(1)
            elif access != "private":
                references.append(
                    (
                        line_number(code, match.start()),
                        f"GpuTaskGraphSubmitter/{access} submitPacket",
                    )
                )
    return sorted(references)


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
            "public packet submitter",
            "class GpuTaskGraphSubmitter final{\n"
            "public:\n"
            "    bool submitPacket(int packet);\n"
            "};",
            ((3, "GpuTaskGraphSubmitter/public submitPacket"),),
        ),
        (
            "protected packet submitter",
            "class GpuTaskGraphSubmitter{\n"
            "protected:\n"
            "    bool submitPacket(int packet);\n"
            "};",
            ((3, "GpuTaskGraphSubmitter/protected submitPacket"),),
        ),
        (
            "reopened public section",
            "class GpuTaskGraphSubmitter{\n"
            "private:\n"
            "    bool prepare();\n"
            "public:\n"
            "    bool submitPacket(int packet);\n"
            "};",
            ((5, "GpuTaskGraphSubmitter/public submitPacket"),),
        ),
        (
            "default public packet submitter struct",
            "struct GpuTaskGraphSubmitter final{\n"
            "    bool submitPacket(int packet);\n"
            "};",
            ((2, "GpuTaskGraphSubmitter/public submitPacket"),),
        ),
        (
            "public inherited packet submitter",
            "class GpuTaskGraphSubmitter final{\n"
            "public:\n"
            "    using Base::submitPacket;\n"
            "};",
            ((3, "GpuTaskGraphSubmitter/public submitPacket"),),
        ),
        (
            "private packet submitter",
            "class GpuTaskGraphSubmitter final{\n"
            "private:\n"
            "    bool submitPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "default private packet submitter",
            "class GpuTaskGraphSubmitter final{\n"
            "    bool submitPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "private inherited packet submitter",
            "class GpuTaskGraphSubmitter final{\n"
            "private:\n"
            "    using Base::submitPacket;\n"
            "};",
            (),
        ),
        (
            "public range submitters",
            "class GpuTaskGraphSubmitter final{\n"
            "public:\n"
            "    bool submitPacketRangeInCompileOrder();\n"
            "    bool submitTaskRangeInCompileOrder();\n"
            "};",
            (),
        ),
        (
            "private native primitive",
            "class GpuTaskGraphSubmitter final{\n"
            "private:\n"
            "    bool submitPacketWithinSubmissionOperation();\n"
            "};",
            (),
        ),
        (
            "nested fixture method",
            "class GpuTaskGraphSubmitter final{\n"
            "public:\n"
            "    struct Fixture{\n"
            "    public:\n"
            "        bool submitPacket();\n"
            "    };\n"
            "private:\n"
            "    bool submitPacketWithinSubmissionOperation();\n"
            "};",
            (),
        ),
        (
            "inline implementation detail",
            "class GpuTaskGraphSubmitter final{\n"
            "public:\n"
            "    bool submit(){\n"
            "        return helper.submitPacket();\n"
            "    }\n"
            "private:\n"
            "    bool submitPacketWithinSubmissionOperation();\n"
            "};",
            (),
        ),
        (
            "comment and literal",
            "// class GpuTaskGraphSubmitter{ public: bool submitPacket(); };\n"
            'const char* text = "GpuTaskGraphSubmitter submitPacket";',
            (),
        ),
        (
            "near names",
            "class GpuTaskGraphSubmitterFactory{ public: bool submitPacket(); };\n"
            "class GpuTaskGraphSubmitter{ public: bool submitPackets(); };",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_public_single_packet_submission(source))
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
        for line, identifier in find_public_single_packet_submission(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: public single-packet submission facade '{identifier}'"
            )

    if violations:
        print(
            "Native packet selection and submission must stay private to the graph runtime.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
