#!/usr/bin/env python3
"""Keep native single-packet recording private to the graph runtime."""

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
RETIRED_RECORD_DESCRIPTOR = re.compile(r"\bGpuNativePacketRecordDesc\b")
RECORDER_CLASS_OPEN = re.compile(r"\b(class|struct)\s+GpuNativePacketRecorder\b[^;{]*\{")
RECORDER_MEMBER_TOKEN = re.compile(
    r"\b(?:(public|private|protected)\s*:|(recordPacket)\b)"
)


def recorder_body_ranges(code: str) -> list[tuple[int, int, str]]:
    ranges: list[tuple[int, int, str]] = []
    for match in RECORDER_CLASS_OPEN.finditer(code):
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


def find_public_single_packet_recording(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_RECORD_DESCRIPTOR.finditer(code)
    ]
    for start, end, default_access in recorder_body_ranges(code):
        access = default_access
        for match in RECORDER_MEMBER_TOKEN.finditer(code, start, end):
            if not is_class_body_top_level(code, start, match.start()):
                continue
            if match.group(1):
                access = match.group(1)
            elif access != "private":
                references.append(
                    (
                        line_number(code, match.start()),
                        f"GpuNativePacketRecorder/{access} recordPacket",
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
            "retired descriptor",
            "GpuNativePacketRecordDesc desc;",
            ((1, "GpuNativePacketRecordDesc"),),
        ),
        (
            "public packet recorder",
            "class GpuNativePacketRecorder final{\n"
            "public:\n"
            "    bool recordPacket(int packet);\n"
            "};",
            ((3, "GpuNativePacketRecorder/public recordPacket"),),
        ),
        (
            "protected packet recorder",
            "class GpuNativePacketRecorder{\n"
            "protected:\n"
            "    bool recordPacket(int packet);\n"
            "};",
            ((3, "GpuNativePacketRecorder/protected recordPacket"),),
        ),
        (
            "reopened public section",
            "class GpuNativePacketRecorder{\n"
            "private:\n"
            "    bool prepare();\n"
            "public:\n"
            "    bool recordPacket(int packet);\n"
            "};",
            ((5, "GpuNativePacketRecorder/public recordPacket"),),
        ),
        (
            "private packet recorder",
            "class GpuNativePacketRecorder final{\n"
            "private:\n"
            "    bool recordPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "default private packet recorder",
            "class GpuNativePacketRecorder final{\n"
            "    bool recordPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "default public packet recorder struct",
            "struct GpuNativePacketRecorder final{\n"
            "    bool recordPacket(int packet);\n"
            "};",
            ((2, "GpuNativePacketRecorder/public recordPacket"),),
        ),
        (
            "public inherited packet recorder",
            "class GpuNativePacketRecorder final{\n"
            "public:\n"
            "    using Base::recordPacket;\n"
            "};",
            ((3, "GpuNativePacketRecorder/public recordPacket"),),
        ),
        (
            "public range recorders",
            "class GpuNativePacketRecorder final{\n"
            "public:\n"
            "    bool recordPacketRangeInCompileOrder();\n"
            "    bool recordTaskRangeInCompileOrder();\n"
            "};",
            (),
        ),
        (
            "nested fixture method",
            "class GpuNativePacketRecorder final{\n"
            "public:\n"
            "    struct Fixture{\n"
            "    public:\n"
            "        bool recordPacket();\n"
            "    };\n"
            "private:\n"
            "    bool recordPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "inline implementation detail",
            "class GpuNativePacketRecorder final{\n"
            "public:\n"
            "    bool record(){\n"
            "        return helper.recordPacket();\n"
            "    }\n"
            "private:\n"
            "    bool recordPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "comment and literal",
            "// GpuNativePacketRecordDesc\n"
            "// class GpuNativePacketRecorder{ public: bool recordPacket(); };\n"
            'const char* text = "GpuNativePacketRecordDesc recordPacket";',
            (),
        ),
        (
            "near names",
            "GpuNativePacketRecordDescription desc;\n"
            "class GpuNativePacketRecorderFactory{ public: bool recordPacket(); };",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_public_single_packet_recording(source))
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
        for line, identifier in find_public_single_packet_recording(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: public single-packet recording facade '{identifier}'"
            )

    if violations:
        print(
            "Native packet selection and recording-worker routing must stay private to the graph runtime.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
