#!/usr/bin/env python3
"""Keep native single-packet submission and rejection private to the graph runtime."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from return_value_handling import blank_non_code, line_number, matching_parenthesis


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
RETIRED_PACKET_RUNTIME_TYPES = re.compile(r"\b(?:GpuPacketRuntimeState|GpuPacketRuntime)\b")
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
        "PacketRuntimeState",
        "PacketRuntime",
        "beginPacketSubmission",
        "acceptSubmittingPacket",
        "rejectPacket",
        "rejectSubmittingPacket",
        "appendAcceptedQueueFrontierWaitTokens",
    ),
}
FORBIDDEN_PUBLIC_MEMBER_ARITIES = {
    "GpuGraphSubmissionTransaction": {
        "discardUnaccepted": frozenset((2,)),
        "rejectTask": frozenset((3,)),
    },
}
CLASS_MEMBER_TOKENS = {
    class_name: re.compile(
        r"\b(?:(public|private|protected)\s*:|("
        + "|".join(
            re.escape(member)
            for member in members + tuple(FORBIDDEN_PUBLIC_MEMBER_ARITIES.get(class_name, {}))
        )
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


def function_parameter_count(code: str, opening: int, closing: int) -> int:
    parameters = code[opening + 1 : closing]
    if not parameters.strip() or parameters.strip() == "void":
        return 0

    count = 1
    parenthesis_depth = 0
    bracket_depth = 0
    brace_depth = 0
    angle_depth = 0
    for character in parameters:
        if character == "(":
            parenthesis_depth += 1
        elif character == ")" and parenthesis_depth:
            parenthesis_depth -= 1
        elif character == "[":
            bracket_depth += 1
        elif character == "]" and bracket_depth:
            bracket_depth -= 1
        elif character == "{":
            brace_depth += 1
        elif character == "}" and brace_depth:
            brace_depth -= 1
        elif character == "<":
            angle_depth += 1
        elif character == ">" and angle_depth:
            angle_depth -= 1
        elif character == "," and not (parenthesis_depth or bracket_depth or brace_depth or angle_depth):
            count += 1
    return count


def is_using_declaration(code: str, class_start: int, offset: int) -> bool:
    statement_start = max(
        code.rfind(";", class_start, offset),
        code.rfind("{", class_start, offset),
        code.rfind("}", class_start, offset),
    )
    return re.search(r"\busing\b", code[statement_start + 1 : offset]) is not None


def find_public_single_packet_submission(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_SUBMISSION_LEASE.finditer(code)
    ]
    references.extend(
        (line_number(code, match.start()), match.group())
        for match in RETIRED_PACKET_RUNTIME_TYPES.finditer(code)
    )
    for class_name, start, end, default_access in target_class_body_ranges(code):
        access = default_access
        for match in CLASS_MEMBER_TOKENS[class_name].finditer(code, start, end):
            if not is_class_body_top_level(code, start, match.start()):
                continue
            if match.group(1):
                access = match.group(1)
                continue
            if access == "private":
                continue

            member = match.group(2)
            forbidden_arities = FORBIDDEN_PUBLIC_MEMBER_ARITIES.get(class_name, {}).get(member)
            if forbidden_arities is None:
                references.append(
                    (line_number(code, match.start()), f"{class_name}/{access} {member}")
                )
                continue

            opening = match.end()
            while opening < end and code[opening].isspace():
                opening += 1
            if opening >= end or code[opening] != "(":
                if is_using_declaration(code, start, match.start()):
                    references.append(
                        (line_number(code, match.start()), f"{class_name}/{access} {member}/inherited")
                    )
                continue
            closing = matching_parenthesis(code, opening)
            if closing is None or closing > end:
                continue
            parameter_count = function_parameter_count(code, opening, closing)
            if parameter_count in forbidden_arities:
                references.append(
                    (line_number(code, match.start()), f"{class_name}/{access} {member}/{parameter_count}")
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
            "retired packet runtime state",
            "GpuPacketRuntimeState state;",
            ((1, "GpuPacketRuntimeState"),),
        ),
        (
            "retired packet runtime",
            "GpuPacketRuntime runtime;",
            ((1, "GpuPacketRuntime"),),
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
            "    enum class PacketRuntimeState : u8;\n"
            "    struct PacketRuntime;\n"
            "    bool beginPacketSubmission();\n"
            "    bool acceptSubmittingPacket();\n"
            "    void rejectPacket();\n"
            "    void rejectSubmittingPacket();\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
            "};",
            (
                (3, "GpuGraphSubmissionTransaction/public PacketRuntimeState"),
                (4, "GpuGraphSubmissionTransaction/public PacketRuntime"),
                (5, "GpuGraphSubmissionTransaction/public beginPacketSubmission"),
                (6, "GpuGraphSubmissionTransaction/public acceptSubmittingPacket"),
                (7, "GpuGraphSubmissionTransaction/public rejectPacket"),
                (8, "GpuGraphSubmissionTransaction/public rejectSubmittingPacket"),
                (9, "GpuGraphSubmissionTransaction/public appendAcceptedQueueFrontierWaitTokens"),
            ),
        ),
        (
            "public generation inferred transaction cleanup",
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&);\n"
            "};",
            (
                (3, "GpuGraphSubmissionTransaction/public rejectTask/3"),
                (4, "GpuGraphSubmissionTransaction/public discardUnaccepted/2"),
            ),
        ),
        (
            "protected generation inferred transaction cleanup",
            "class GpuGraphSubmissionTransaction final{\n"
            "protected:\n"
            "    void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&);\n"
            "};",
            (
                (3, "GpuGraphSubmissionTransaction/protected rejectTask/3"),
                (4, "GpuGraphSubmissionTransaction/protected discardUnaccepted/2"),
            ),
        ),
        (
            "default public generation inferred transaction cleanup",
            "struct GpuGraphSubmissionTransaction{\n"
            "    void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&);\n"
            "};",
            (
                (2, "GpuGraphSubmissionTransaction/public rejectTask/3"),
                (3, "GpuGraphSubmissionTransaction/public discardUnaccepted/2"),
            ),
        ),
        (
            "nested declarator commas retain transaction cleanup arity",
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    void rejectTask(GpuTaskGraph&, const Pair<GpuCompiledGraph, GpuSubmissionPacketId>&, void (*)(u32, u32));\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const Array<Pair<u32, u32>, 2u>&);\n"
            "};",
            (
                (3, "GpuGraphSubmissionTransaction/public rejectTask/3"),
                (4, "GpuGraphSubmissionTransaction/public discardUnaccepted/2"),
            ),
        ),
        (
            "protected transaction packet rejection",
            "class GpuGraphSubmissionTransaction{\n"
            "protected:\n"
            "    enum class PacketRuntimeState : u8;\n"
            "    struct PacketRuntime;\n"
            "    void rejectPacket();\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
            "};",
            (
                (3, "GpuGraphSubmissionTransaction/protected PacketRuntimeState"),
                (4, "GpuGraphSubmissionTransaction/protected PacketRuntime"),
                (5, "GpuGraphSubmissionTransaction/protected rejectPacket"),
                (6, "GpuGraphSubmissionTransaction/protected appendAcceptedQueueFrontierWaitTokens"),
            ),
        ),
        (
            "default public transaction struct",
            "struct GpuGraphSubmissionTransaction{\n"
            "    enum class PacketRuntimeState : u8;\n"
            "    struct PacketRuntime;\n"
            "    void rejectPacket();\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
            "};",
            (
                (2, "GpuGraphSubmissionTransaction/public PacketRuntimeState"),
                (3, "GpuGraphSubmissionTransaction/public PacketRuntime"),
                (4, "GpuGraphSubmissionTransaction/public rejectPacket"),
                (5, "GpuGraphSubmissionTransaction/public appendAcceptedQueueFrontierWaitTokens"),
            ),
        ),
        (
            "private transaction packet lifecycle",
            "class GpuGraphSubmissionTransaction final{\n"
            "private:\n"
            "    enum class PacketRuntimeState : u8;\n"
            "    struct PacketRuntime;\n"
            "    bool beginPacketSubmission();\n"
            "    bool acceptSubmittingPacket();\n"
            "    void rejectPacket();\n"
            "    void rejectSubmittingPacket();\n"
            "    void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&);\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
            "};",
            (),
        ),
        (
            "default private transaction packet rejection",
            "class GpuGraphSubmissionTransaction final{\n"
            "    enum class PacketRuntimeState : u8;\n"
            "    struct PacketRuntime;\n"
            "    void rejectPacket();\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
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
            "public explicit attempt semantic cancellation APIs",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    bool discardUnaccepted();\n"
            "};\n"
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId, u64);\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&, u64);\n"
            "};",
            (),
        ),
        (
            "other public semantic cancellation arities",
            "class GpuGraphSubmissionTransaction final{\n"
            "public:\n"
            "    void rejectTask();\n"
            "    bool discardUnaccepted(GpuTaskGraph&);\n"
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
            "    using Base::PacketRuntimeState;\n"
            "    using Base::PacketRuntime;\n"
            "    using Base::rejectPacket;\n"
            "    using Base::rejectTask;\n"
            "    using Base::discardUnaccepted;\n"
            "    using Base::appendAcceptedQueueFrontierWaitTokens;\n"
            "};",
            (
                (3, "GpuTaskGraph/public discardUnacceptedPacket"),
                (7, "GpuGraphSubmissionTransaction/public PacketRuntimeState"),
                (8, "GpuGraphSubmissionTransaction/public PacketRuntime"),
                (9, "GpuGraphSubmissionTransaction/public rejectPacket"),
                (10, "GpuGraphSubmissionTransaction/public rejectTask/inherited"),
                (11, "GpuGraphSubmissionTransaction/public discardUnaccepted/inherited"),
                (12, "GpuGraphSubmissionTransaction/public appendAcceptedQueueFrontierWaitTokens"),
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
            "        enum class PacketRuntimeState : u8;\n"
            "        struct PacketRuntime;\n"
            "        void rejectPacket();\n"
            "        void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "        bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&);\n"
            "        bool appendAcceptedQueueFrontierWaitTokens();\n"
            "    };\n"
            "private:\n"
            "    enum class PacketRuntimeState : u8;\n"
            "    struct PacketRuntime;\n"
            "    void rejectPacket();\n"
            "    void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "    bool discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&);\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
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
            "    bool reject(){ return helper.rejectPacket() && helper.rejectTask(graph, compiledGraph, task); }\n"
            "    bool discard(){ return helper.discardUnaccepted(graph, compiledGraph); }\n"
            "    bool appendFrontier(){ return helper.appendAcceptedQueueFrontierWaitTokens(queue, tokens); }\n"
            "private:\n"
            "    void rejectPacket();\n"
            "    bool appendAcceptedQueueFrontierWaitTokens();\n"
            "};",
            (),
        ),
        (
            "out of class definitions",
            "bool GpuTaskGraph::discardUnacceptedPacket(){ return true; }\n"
            "void GpuGraphSubmissionTransaction::rejectPacket(){}\n"
            "void GpuGraphSubmissionTransaction::rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId){}\n"
            "bool GpuGraphSubmissionTransaction::discardUnaccepted(GpuTaskGraph&, const GpuCompiledGraph&){ return true; }\n"
            "bool GpuGraphSubmissionTransaction::appendAcceptedQueueFrontierWaitTokens(){ return true; }",
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
            "// GpuPacketRuntimeState GpuPacketRuntime\n"
            "// class GpuTaskGraph{ public: class PacketSubmissionLease; };\n"
            "// class GpuGraphSubmissionTransaction{ public: void rejectPacket(); };\n"
            "// void rejectTask(GpuTaskGraph&, const GpuCompiledGraph&, GpuTaskId);\n"
            "// bool appendAcceptedQueueFrontierWaitTokens();\n"
            'const char* text = "GpuTaskGraphSubmitter GpuPacketRuntime submitPacket discardUnacceptedPacket discardUnaccepted(a, b) appendAcceptedQueueFrontierWaitTokens";\n'
            'const char* raw = R"tag(GpuTaskPacketSubmissionLease GpuPacketRuntimeState rejectPacket rejectTask(a, b, c) appendAcceptedQueueFrontierWaitTokens)tag";',
            (),
        ),
        (
            "near names",
            "GpuTaskPacketSubmissionLeaseFactory lease;\n"
            "GpuPacketRuntimeStates state;\n"
            "GpuPacketRuntimes runtime;\n"
            "class GpuTaskGraphSubmitterFactory{ public: bool submitPacket(); };\n"
            "class GpuTaskGraphSubmitter{ public: bool submitPackets(); };\n"
            "class GpuTaskGraphFactory{ public: bool discardUnacceptedPacket(); };\n"
            "class GpuTaskGraph{ public: class PacketSubmissionLeases; bool discardUnacceptedPackets(); };\n"
            "class GpuGraphSubmissionTransactionFactory{ public: struct PacketRuntime; bool rejectPacket(); bool appendAcceptedQueueFrontierWaitTokens(); };\n"
            "class GpuGraphSubmissionTransaction{ public: struct PacketRuntimes; bool rejectPackets(); void rejectTasks(int, int, int); bool discardUnaccepteds(int, int); bool appendAcceptedQueueFrontierWaitTokenSets(); };\n"
            "class Fixture{ public: struct PacketRuntime; enum class PacketRuntimeState : u8; bool discardUnacceptedPacket(); bool rejectPacket(); void rejectTask(int, int, int); bool discardUnaccepted(int, int); bool appendAcceptedQueueFrontierWaitTokens(); };",
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
