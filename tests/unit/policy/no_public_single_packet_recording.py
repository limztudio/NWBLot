#!/usr/bin/env python3
"""Keep native single-packet recording and lifecycle capabilities private."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from policy_scan import REPOSITORY_ROOT, SOURCE_SUFFIXES, blank_non_code, first_party_source_files, line_number
RETIRED_RECORD_DESCRIPTOR = re.compile(r"\bGpuNativePacketRecordDesc\b")
RETIRED_RECORDING_LEASE = re.compile(r"\bGpuTaskPacketRecordingLease\b")
RETIRED_TASK_LIFECYCLE_STATE = re.compile(r"\bGpuTaskLifecycleState\b")
TARGET_CLASS_OPEN = re.compile(
    r"\b(class|struct)\s+"
    r"(GpuNativePacketRecorder|GpuTaskGraph|GpuRecordedGraph)\b"
    r"(?!\s*::)[^;{]*\{"
)
PRIVATE_RECORDING_MEMBERS = {
    "GpuNativePacketRecorder": ("prepareRecordingAttempt", "recordPacket"),
    "GpuTaskGraph": (
        "TaskLifecycleState",
        "PacketRecordingLease",
        "recordingAttemptGeneration",
        "beginRecordingAttempt",
        "matchesRecordingAttempt",
        "recordTask",
        "applyCompiledBarrier",
        "seedTaskRetainedResourceStates",
        "beginPacketRecording",
        "completePacketRecording",
        "abortPacketRecording",
        "packetReadyForSubmission",
    ),
    "GpuRecordedGraph": ("resetForRecording",),
}
CLASS_MEMBER_TOKENS = {
    class_name: re.compile(
        r"\b(?:(public|private|protected)\s*:|("
        + "|".join(re.escape(member) for member in members)
        + r")\b)"
    )
    for class_name, members in PRIVATE_RECORDING_MEMBERS.items()
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


def find_public_single_packet_recording(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_RECORD_DESCRIPTOR.finditer(code)
    ]
    references.extend(
        (line_number(code, match.start()), match.group())
        for match in RETIRED_RECORDING_LEASE.finditer(code)
    )
    references.extend(
        (line_number(code, match.start()), match.group())
        for match in RETIRED_TASK_LIFECYCLE_STATE.finditer(code)
    )
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
    return first_party_source_files(source_root)


def run_self_test() -> int:
    cases = (
        (
            "retired descriptor",
            "GpuNativePacketRecordDesc desc;",
            ((1, "GpuNativePacketRecordDesc"),),
        ),
        (
            "retired recording lease",
            "GpuTaskPacketRecordingLease lease;",
            ((1, "GpuTaskPacketRecordingLease"),),
        ),
        (
            "retired task lifecycle state",
            "GpuTaskLifecycleState state;",
            ((1, "GpuTaskLifecycleState"),),
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
            "    bool prepareRecordingAttempt();\n"
            "    bool recordPacket(int packet);\n"
            "};",
            (
                (3, "GpuNativePacketRecorder/protected prepareRecordingAttempt"),
                (4, "GpuNativePacketRecorder/protected recordPacket"),
            ),
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
            "    bool prepareRecordingAttempt();\n"
            "    bool recordPacket(int packet);\n"
            "};",
            (),
        ),
        (
            "default private packet recorder",
            "class GpuNativePacketRecorder final{\n"
            "    bool prepareRecordingAttempt();\n"
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
            "public task graph recording internals",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    enum class TaskLifecycleState : u8;\n"
            "    class PacketRecordingLease;\n"
            "    bool recordTask();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
            "};",
            (
                (3, "GpuTaskGraph/public TaskLifecycleState"),
                (4, "GpuTaskGraph/public PacketRecordingLease"),
                (5, "GpuTaskGraph/public recordTask"),
                (6, "GpuTaskGraph/public applyCompiledBarrier"),
                (7, "GpuTaskGraph/public seedTaskRetainedResourceStates"),
            ),
        ),
        (
            "public task graph recording lifecycle",
            "class GpuTaskGraph final : NoCopy{\n"
            "public:\n"
            "    u64 recordingAttemptGeneration()const;\n"
            "    bool beginRecordingAttempt();\n"
            "    bool matchesRecordingAttempt();\n"
            "    bool beginPacketRecording();\n"
            "    bool completePacketRecording();\n"
            "    void abortPacketRecording();\n"
            "    bool packetReadyForSubmission();\n"
            "};",
            (
                (3, "GpuTaskGraph/public recordingAttemptGeneration"),
                (4, "GpuTaskGraph/public beginRecordingAttempt"),
                (5, "GpuTaskGraph/public matchesRecordingAttempt"),
                (6, "GpuTaskGraph/public beginPacketRecording"),
                (7, "GpuTaskGraph/public completePacketRecording"),
                (8, "GpuTaskGraph/public abortPacketRecording"),
                (9, "GpuTaskGraph/public packetReadyForSubmission"),
            ),
        ),
        (
            "protected task graph recording lifecycle",
            "class GpuTaskGraph{\n"
            "protected:\n"
            "    enum class TaskLifecycleState : u8;\n"
            "    class PacketRecordingLease;\n"
            "    bool beginPacketRecording();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
            "};",
            (
                (3, "GpuTaskGraph/protected TaskLifecycleState"),
                (4, "GpuTaskGraph/protected PacketRecordingLease"),
                (5, "GpuTaskGraph/protected beginPacketRecording"),
                (6, "GpuTaskGraph/protected applyCompiledBarrier"),
                (7, "GpuTaskGraph/protected seedTaskRetainedResourceStates"),
            ),
        ),
        (
            "default public task graph struct",
            "struct GpuTaskGraph final : Base{\n"
            "    enum class TaskLifecycleState : u8;\n"
            "    class PacketRecordingLease;\n"
            "    bool recordTask();\n"
            "    bool packetReadyForSubmission();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
            "};",
            (
                (2, "GpuTaskGraph/public TaskLifecycleState"),
                (3, "GpuTaskGraph/public PacketRecordingLease"),
                (4, "GpuTaskGraph/public recordTask"),
                (5, "GpuTaskGraph/public packetReadyForSubmission"),
                (6, "GpuTaskGraph/public applyCompiledBarrier"),
                (7, "GpuTaskGraph/public seedTaskRetainedResourceStates"),
            ),
        ),
        (
            "reopened public task graph section",
            "class GpuTaskGraph final{\n"
            "private:\n"
            "    bool beginRecordingAttempt();\n"
            "public:\n"
            "    bool matchesRecordingAttempt();\n"
            "};",
            ((5, "GpuTaskGraph/public matchesRecordingAttempt"),),
        ),
        (
            "public inherited task graph lifecycle",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    using Base::beginPacketRecording;\n"
            "    using Base::TaskLifecycleState;\n"
            "    using Base::applyCompiledBarrier;\n"
            "    using Base::seedTaskRetainedResourceStates;\n"
            "};",
            (
                (3, "GpuTaskGraph/public beginPacketRecording"),
                (4, "GpuTaskGraph/public TaskLifecycleState"),
                (5, "GpuTaskGraph/public applyCompiledBarrier"),
                (6, "GpuTaskGraph/public seedTaskRetainedResourceStates"),
            ),
        ),
        (
            "private task graph recording lifecycle",
            "class GpuTaskGraph final{\n"
            "private:\n"
            "    enum class TaskLifecycleState : u8;\n"
            "    class PacketRecordingLease;\n"
            "    u64 recordingAttemptGeneration()const;\n"
            "    bool beginRecordingAttempt();\n"
            "    bool matchesRecordingAttempt();\n"
            "    bool recordTask();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
            "    bool beginPacketRecording();\n"
            "    bool completePacketRecording();\n"
            "    void abortPacketRecording();\n"
            "    bool packetReadyForSubmission();\n"
            "};",
            (),
        ),
        (
            "default private task graph lifecycle",
            "class GpuTaskGraph final{\n"
            "    enum class TaskLifecycleState : u8;\n"
            "    bool beginPacketRecording();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
            "};",
            (),
        ),
        (
            "public recorded graph reset for recording",
            "class GpuRecordedGraph final{\n"
            "public:\n"
            "    void resetForRecording();\n"
            "};",
            ((3, "GpuRecordedGraph/public resetForRecording"),),
        ),
        (
            "protected recorded graph reset for recording",
            "class GpuRecordedGraph{\n"
            "protected:\n"
            "    void resetForRecording();\n"
            "};",
            ((3, "GpuRecordedGraph/protected resetForRecording"),),
        ),
        (
            "default public recorded graph struct",
            "struct GpuRecordedGraph final{\n"
            "    void resetForRecording();\n"
            "};",
            ((2, "GpuRecordedGraph/public resetForRecording"),),
        ),
        (
            "private recorded graph reset for recording",
            "class GpuRecordedGraph final{\n"
            "private:\n"
            "    void resetForRecording();\n"
            "};",
            (),
        ),
        (
            "out of class definitions",
            "bool GpuTaskGraph::beginRecordingAttempt(){ return true; }\n"
            "bool GpuTaskGraph::packetReadyForSubmission(){ return true; }\n"
            "bool GpuTaskGraph::applyCompiledBarrier(){ return true; }\n"
            "bool GpuTaskGraph::seedTaskRetainedResourceStates(){ return true; }\n"
            "void GpuRecordedGraph::resetForRecording(){}",
            (),
        ),
        (
            "legitimate attempt queries",
            "class GpuRecordedGraph final{\n"
            "public:\n"
            "    u64 recordingAttemptGeneration()const;\n"
            "};\n"
            "class GpuCommandIrCapture final{\n"
            "public:\n"
            "    u64 beginRecordingAttempt();\n"
            "    u64 recordingAttemptGeneration()const;\n"
            "};\n"
            "class StateTracker final{\n"
            "public:\n"
            "    bool beginRecordingAttempt();\n"
            "};",
            (),
        ),
        (
            "unrelated same named methods",
            "class RecordingFixture final{\n"
            "public:\n"
            "    bool beginPacketRecording();\n"
            "    void resetForRecording();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
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
            "nested task graph fixture method",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    struct Fixture{\n"
            "    public:\n"
            "        enum class TaskLifecycleState : u8;\n"
            "        class PacketRecordingLease;\n"
            "        bool recordTask();\n"
            "        bool beginPacketRecording();\n"
            "        bool applyCompiledBarrier();\n"
            "        bool seedTaskRetainedResourceStates();\n"
            "    };\n"
            "    bool recordTaskRange();\n"
            "private:\n"
            "    enum class TaskLifecycleState : u8;\n"
            "    class PacketRecordingLease;\n"
            "    bool recordTask();\n"
            "    bool beginPacketRecording();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
            "};",
            (),
        ),
        (
            "inline task graph implementation detail",
            "class GpuTaskGraph final{\n"
            "public:\n"
            "    bool recordTaskRange(){\n"
            "        return helper.beginPacketRecording();\n"
            "    }\n"
            "    bool lowerBarriers(){ return helper.applyCompiledBarrier(); }\n"
            "    bool seedRetainedStates(){ return helper.seedTaskRetainedResourceStates(); }\n"
            "private:\n"
            "    bool beginPacketRecording();\n"
            "    bool applyCompiledBarrier();\n"
            "    bool seedTaskRetainedResourceStates();\n"
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
            "// GpuTaskPacketRecordingLease\n"
            "// GpuTaskLifecycleState\n"
            "// class GpuTaskGraph{ public: bool beginPacketRecording(); };\n"
            "// bool applyCompiledBarrier(); bool seedTaskRetainedResourceStates();\n"
            'const char* text = "GpuNativePacketRecordDesc GpuTaskLifecycleState resetForRecording applyCompiledBarrier";\n'
            'const char* raw = R"tag(GpuTaskPacketRecordingLease TaskLifecycleState beginRecordingAttempt seedTaskRetainedResourceStates)tag";',
            (),
        ),
        (
            "near names",
            "GpuNativePacketRecordDescription desc;\n"
            "GpuTaskPacketRecordingLeaseFactory lease;\n"
            "GpuTaskLifecycleStates state;\n"
            "class GpuNativePacketRecorderFactory{ public: bool recordPacket(); };\n"
            "class GpuTaskGraphFactory{ public: enum class TaskLifecycleState : u8; bool beginPacketRecordingRange(); bool applyCompiledBarriers(); };\n"
            "class GpuTaskGraph{ public: bool seedTaskRetainedResourceStateSets(); };\n"
            "class Fixture{ public: enum class TaskLifecycleState : u8; bool applyCompiledBarrier(); bool seedTaskRetainedResourceStates(); };\n"
            "class GpuRecordedGraphFactory{ public: bool resetForRecordings(); };",
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
            "Native packet selection, recording lifecycle, and recording-worker routing must stay private to the graph runtime.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
