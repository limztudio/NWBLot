#!/usr/bin/env python3
"""Keep native single-packet submission and rejection private to the graph runtime."""

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
RETIRED_SUBMISSION_LEASE = re.compile(r"\bGpuTaskPacketSubmissionLease\b")
TARGET_CLASS_OPEN = re.compile(
    r"\b(class|struct)\s+"
    r"(GpuTaskGraphSubmitter|GpuTaskGraph|GpuGraphSubmissionTransaction)\b"
    r"(?!\s*::)[^;{]*\{"
)
PRIVATE_SUBMISSION_MEMBERS = {
    "GpuTaskGraphSubmitter": ("submitPacket",),
    "GpuTaskGraph": (
        "PacketSubmissionLease",
        "beginPacketSubmission",
        "completePacketSubmission",
        "abortPacketSubmission",
        "discardUnacceptedPacket",
    ),
    "GpuGraphSubmissionTransaction": (
        "beginPacketSubmission",
        "acceptSubmittingPacket",
        "rejectPacket",
        "rejectSubmittingPacket",
    ),
}
CLASS_MEMBER_TOKENS = {
    class_name: re.compile(
        r"\b(?:(public|private|protected)\s*:|("
        + "|".join(re.escape(member) for member in members)
        + r")\b)"
    )
    for class_name, members in PRIVATE_SUBMISSION_MEMBERS.items()
}


def target_class_body_ranges(code: str) -> list[tuple[str, int, int, str]]:
    ranges: list[tuple[str, int, int, str]] = []
    for match in TARGET_CLASS_OPEN.finditer(code):
        open_offset = match.end() - 1
        default_access = "public" if match.group(1) == "struct" else "private"
        depth = 0
        for offset in range(open_offset, len(code)):
            if code[offset] == "{":
                depth += 1
            elif code[offset] == "}":
                depth -= 1
                if depth == 0:
                    ranges.append((match.group(2), open_offset + 1, offset, default_access))
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
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_SUBMISSION_LEASE.finditer(code)
    ]
    for class_name, start, end, default_access in target_class_body_ranges(code):
        access = default_access
        for match in CLASS_MEMBER_TOKENS[class_name].finditer(code, start, end):
            if not is_class_body_top_level(code, start, match.start()):
                continue
            if match.group(1):
                access = match.group(1)
            elif access != "private":
                references.append(
                    (
                        line_number(code, match.start()),
                        f"{class_name}/{access} {match.group(2)}",
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
            "retired submission lease",
            "GpuTaskPacketSubmissionLease lease;",
            ((1, "GpuTaskPacketSubmissionLease"),),
        ),
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
            "public task graph submission lifecycle",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    class PacketSubmissionLease;\n"
            "    bool beginPacketSubmission();\n"
            "    bool completePacketSubmission();\n"
            "    void abortPacketSubmission();\n"
            "    bool discardUnacceptedPacket();\n"
            "};",
            (
                (3, "GpuTaskGraph/public PacketSubmissionLease"),
                (4, "GpuTaskGraph/public beginPacketSubmission"),
                (5, "GpuTaskGraph/public completePacketSubmission"),
                (6, "GpuTaskGraph/public abortPacketSubmission"),
                (7, "GpuTaskGraph/public discardUnacceptedPacket"),
            ),
        ),
        (
            "protected task graph submission lifecycle",
            "class GpuTaskGraph{\n"
            "protected:\n"
            "    class PacketSubmissionLease;\n"
            "    bool discardUnacceptedPacket();\n"
            "};",
            (
                (3, "GpuTaskGraph/protected PacketSubmissionLease"),
                (4, "GpuTaskGraph/protected discardUnacceptedPacket"),
            ),
        ),
        (
            "default public task graph struct",
            "struct GpuTaskGraph final{\n"
            "    class PacketSubmissionLease;\n"
            "    bool beginPacketSubmission();\n"
            "};",
            (
                (2, "GpuTaskGraph/public PacketSubmissionLease"),
                (3, "GpuTaskGraph/public beginPacketSubmission"),
            ),
        ),
        (
            "private task graph submission lifecycle",
            "class GpuTaskGraph final{\n"
            "private:\n"
            "    class PacketSubmissionLease;\n"
            "    bool beginPacketSubmission();\n"
            "    bool completePacketSubmission();\n"
            "    void abortPacketSubmission();\n"
            "    bool discardUnacceptedPacket();\n"
            "};",
            (),
        ),
        (
            "default private task graph submission lifecycle",
            "class GpuTaskGraph final{\n"
            "    class PacketSubmissionLease;\n"
            "    bool discardUnacceptedPacket();\n"
            "};",
            (),
        ),
        (
            "public transaction packet lifecycle",
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    bool beginPacketSubmission();\n"
            "    bool acceptSubmittingPacket();\n"
            "    void rejectPacket();\n"
            "    void rejectSubmittingPacket();\n"
            "};",
            (
                (3, "GpuGraphSubmissionTransaction/public beginPacketSubmission"),
                (4, "GpuGraphSubmissionTransaction/public acceptSubmittingPacket"),
                (5, "GpuGraphSubmissionTransaction/public rejectPacket"),
                (6, "GpuGraphSubmissionTransaction/public rejectSubmittingPacket"),
            ),
        ),
        (
            "protected transaction packet rejection",
            "class GpuGraphSubmissionTransaction{\n"
            "protected:\n"
            "    void rejectPacket();\n"
            "};",
            ((3, "GpuGraphSubmissionTransaction/protected rejectPacket"),),
        ),
        (
            "default public transaction struct",
            "struct GpuGraphSubmissionTransaction{\n"
            "    void rejectPacket();\n"
            "};",
            ((2, "GpuGraphSubmissionTransaction/public rejectPacket"),),
        ),
        (
            "private transaction packet lifecycle",
            "class GpuGraphSubmissionTransaction final{\n"
            "private:\n"
            "    bool beginPacketSubmission();\n"
            "    bool acceptSubmittingPacket();\n"
            "    void rejectPacket();\n"
            "    void rejectSubmittingPacket();\n"
            "};",
            (),
        ),
        (
            "default private transaction packet rejection",
            "class GpuGraphSubmissionTransaction final{\n"
            "    void rejectPacket();\n"
            "};",
            (),
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
            "public semantic cancellation APIs",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    bool discardUnaccepted();\n"
            "};\n"
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    void rejectTask();\n"
            "    bool discardUnaccepted();\n"
            "};",
            (),
        ),
        (
            "public inherited packet lifecycle",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    using Base::discardUnacceptedPacket;\n"
            "};\n"
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    using Base::rejectPacket;\n"
            "};",
            (
                (3, "GpuTaskGraph/public discardUnacceptedPacket"),
                (7, "GpuGraphSubmissionTransaction/public rejectPacket"),
            ),
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
            "nested task graph and transaction fixtures",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    struct Fixture{\n"
            "    public:\n"
            "        class PacketSubmissionLease;\n"
            "        bool discardUnacceptedPacket();\n"
            "    };\n"
            "private:\n"
            "    class PacketSubmissionLease;\n"
            "    bool discardUnacceptedPacket();\n"
            "};\n"
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    struct Fixture{\n"
            "    public:\n"
            "        void rejectPacket();\n"
            "    };\n"
            "private:\n"
            "    void rejectPacket();\n"
            "};",
            (),
        ),
        (
            "inline implementation details",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    bool discard(){ return helper.discardUnacceptedPacket(); }\n"
            "private:\n"
            "    bool discardUnacceptedPacket();\n"
            "};\n"
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    bool reject(){ return helper.rejectPacket(); }\n"
            "private:\n"
            "    void rejectPacket();\n"
            "};",
            (),
        ),
        (
            "out of class definitions",
            "bool GpuTaskGraph::discardUnacceptedPacket(){ return true; }\n"
            "void GpuGraphSubmissionTransaction::rejectPacket(){}",
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
            "// GpuTaskPacketSubmissionLease\n"
            "// class GpuTaskGraph{ public: class PacketSubmissionLease; };\n"
            "// class GpuGraphSubmissionTransaction{ public: void rejectPacket(); };\n"
            'const char* text = "GpuTaskGraphSubmitter submitPacket discardUnacceptedPacket";\n'
            'const char* raw = R"tag(GpuTaskPacketSubmissionLease rejectPacket)tag";',
            (),
        ),
        (
            "near names",
            "GpuTaskPacketSubmissionLeaseFactory lease;\n"
            "class GpuTaskGraphSubmitterFactory{ public: bool submitPacket(); };\n"
            "class GpuTaskGraphSubmitter{ public: bool submitPackets(); };\n"
            "class GpuTaskGraphFactory{ public: bool discardUnacceptedPacket(); };\n"
            "class GpuTaskGraph{ public: class PacketSubmissionLeases; bool discardUnacceptedPackets(); };\n"
            "class GpuGraphSubmissionTransactionFactory{ public: bool rejectPacket(); };\n"
            "class GpuGraphSubmissionTransaction{ public: bool rejectPackets(); };\n"
            "class Fixture{ public: bool discardUnacceptedPacket(); bool rejectPacket(); };",
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
            "Native packet selection, submission, and rejection lifecycle must stay private to the graph runtime.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
