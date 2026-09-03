#!/usr/bin/env python3
"""Keep retired recording-time packet state overrides out of production sources."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from policy_scan import REPOSITORY_ROOT, blank_non_code, line_number, production_source_files
RETIRED_IDENTIFIER = re.compile(
    r"\b(?:GpuExternalPacketStateSource|GpuTaskPacketStateBinding|recordOverrides|recordOverrideCount|"
    r"taskStateBindings|taskStateBindingCount)\b"
)
NATIVE_PACKET_RECORD_DESC_OPEN = re.compile(
    r"\b(?:class|struct)\s+GpuNativePacketRecordDesc\b[^;{]*\{"
)
RETIRED_NATIVE_PACKET_RECORD_FIELD = re.compile(r"\b(?:externalStateSources|externalStateSourceCount)\b")


def native_packet_record_desc_body_ranges(code: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for match in NATIVE_PACKET_RECORD_DESC_OPEN.finditer(code):
        open_offset = match.end() - 1
        depth = 0
        for offset in range(open_offset, len(code)):
            if code[offset] == "{":
                depth += 1
            elif code[offset] == "}":
                depth -= 1
                if depth == 0:
                    ranges.append((open_offset + 1, offset))
                    break
    return ranges


def is_descriptor_body_top_level(code: str, start: int, offset: int) -> bool:
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


def find_recording_time_packet_state_overrides(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_IDENTIFIER.finditer(code)
    ]
    for start, end in native_packet_record_desc_body_ranges(code):
        for match in RETIRED_NATIVE_PACKET_RECORD_FIELD.finditer(code, start, end):
            if not is_descriptor_body_top_level(code, start, match.start()):
                continue
            references.append((line_number(code, match.start()), f"GpuNativePacketRecordDesc/{match.group()}"))
    return sorted(references)


def run_self_test() -> int:
    cases = (
        (
            "packet state-source type",
            "GpuExternalPacketStateSource source;",
            ((1, "GpuExternalPacketStateSource"),),
        ),
        (
            "task packet-state binding type",
            "GpuTaskPacketStateBinding binding;",
            ((1, "GpuTaskPacketStateBinding"),),
        ),
        (
            "record override names",
            "recordPacketRange(recordOverrides, recordOverrideCount);",
            ((1, "recordOverrideCount"), (1, "recordOverrides")),
        ),
        (
            "task state-binding names",
            "recordPacket(taskStateBindings, taskStateBindingCount);",
            ((1, "taskStateBindingCount"), (1, "taskStateBindings")),
        ),
        (
            "packet record external source",
            "struct GpuNativePacketRecordDesc{\n"
            "    const GpuTaskExternalStateSource* externalStateSources;\n"
            "};",
            ((2, "GpuNativePacketRecordDesc/externalStateSources"),),
        ),
        (
            "inherited final packet record source count",
            "class GpuNativePacketRecordDesc final : public RecordBase{\n"
            "    usize externalStateSourceCount;\n"
            "};",
            ((2, "GpuNativePacketRecordDesc/externalStateSourceCount"),),
        ),
        (
            "declaration-owned task source",
            "GpuTaskExternalStateSource source;\n"
            "struct GpuTaskDesc{\n"
            "    const GpuTaskExternalStateSource* externalStateSources;\n"
            "    usize externalStateSourceCount;\n"
            "};",
            (),
        ),
        (
            "clean packet record descriptor",
            "struct GpuNativePacketRecordDesc{\n"
            "    GpuSubmissionPacketId packet;\n"
            "    u64 recordingWorkerDomain;\n"
            "    u32 recordingWorkerIndex;\n"
            "};",
            (),
        ),
        (
            "nested and local generic external source names",
            "struct GpuNativePacketRecordDesc final{\n"
            "    struct Nested{ const void* externalStateSources; };\n"
            "    void validate(const void* externalStateSources){\n"
            "        usize externalStateSourceCount = 0u;\n"
            "    }\n"
            "};",
            (),
        ),
        (
            "comment",
            "// GpuExternalPacketStateSource recordOverrides taskStateBindings\n"
            "// struct GpuNativePacketRecordDesc{ const void* externalStateSources; };",
            (),
        ),
        (
            "literal",
            'const char* text = "GpuTaskPacketStateBinding recordOverrideCount taskStateBindingCount";\n'
            'const char* raw = R"(struct GpuNativePacketRecordDesc{ usize externalStateSourceCount; };)";',
            (),
        ),
        (
            "near names",
            "GpuExternalPacketStateSourceList sources; GpuTaskPacketStateBindingSet bindings;\n"
            "usize recordOverridesCount = 0u; usize taskStateBindingsCount = 0u;",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_recording_time_packet_state_overrides(source))
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
        for line, identifier in find_recording_time_packet_state_overrides(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired recording-time packet state override '{identifier}'"
            )

    if violations:
        print(
            "Native packet state must come from graph declarations and accepted state, not recording-time overrides.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
