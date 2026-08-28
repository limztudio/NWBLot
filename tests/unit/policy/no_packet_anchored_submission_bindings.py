#!/usr/bin/env python3
"""Keep retired packet-anchored task-graph submission bindings out of production sources."""

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
RETIRED_IDENTIFIER = re.compile(
    r"\b(?:GpuTaskGraphPacketTimingTicket|GpuTaskGraphPacketSubmissionHook|"
    r"GpuTaskGraphPacketAcceptedCallback|submitPacketRangeInCompileOrderFromTasks|"
    r"submitTaskRangeInCompileOrderFromTasks)\b"
)
NORMAL_EXECUTION_DESC_OPEN = re.compile(
    r"\b(?:class|struct)\s+GpuTaskGraphNormalExecutionDesc\b[^;{]*\{"
)
RETIRED_NORMAL_EXECUTION_FIELD = re.compile(r"\b(?:acceptedCallback|submissionHooks|submissionHookCount)\b")
SUBMIT_PACKET_OPEN = re.compile(r"\bsubmitPacket\s*\(")
RETIRED_SUBMIT_PACKET_BINDING = re.compile(r"\b(?:GpuTimingSubmissionTicket|QueueSubmissionPreSubmitHook)\b")


def normal_execution_desc_body_ranges(code: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for match in NORMAL_EXECUTION_DESC_OPEN.finditer(code):
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


def submit_packet_parameter_ranges(code: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for match in SUBMIT_PACKET_OPEN.finditer(code):
        open_offset = match.end() - 1
        depth = 0
        for offset in range(open_offset, len(code)):
            if code[offset] == "(":
                depth += 1
            elif code[offset] == ")":
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


def find_packet_anchored_submission_references(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_IDENTIFIER.finditer(code)
    ]
    for start, end in normal_execution_desc_body_ranges(code):
        for match in RETIRED_NORMAL_EXECUTION_FIELD.finditer(code, start, end):
            if not is_descriptor_body_top_level(code, start, match.start()):
                continue
            references.append((line_number(code, match.start()), match.group()))
    for start, end in submit_packet_parameter_ranges(code):
        for match in RETIRED_SUBMIT_PACKET_BINDING.finditer(code, start, end):
            references.append((line_number(code, match.start()), f"submitPacket/{match.group()}"))
    return sorted(references)


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
            "packet timing type",
            "GpuTaskGraphPacketTimingTicket ticket;",
            ((1, "GpuTaskGraphPacketTimingTicket"),),
        ),
        (
            "packet submission-hook type",
            "GpuTaskGraphPacketSubmissionHook hook;",
            ((1, "GpuTaskGraphPacketSubmissionHook"),),
        ),
        (
            "packet accepted-callback type",
            "GpuTaskGraphPacketAcceptedCallback callback;",
            ((1, "GpuTaskGraphPacketAcceptedCallback"),),
        ),
        (
            "packet-range from-tasks overload",
            "submitPacketRangeInCompileOrderFromTasks(graph, compiledGraph);",
            ((1, "submitPacketRangeInCompileOrderFromTasks"),),
        ),
        (
            "task-range from-tasks overload",
            "submitTaskRangeInCompileOrderFromTasks(graph, compiledGraph);",
            ((1, "submitTaskRangeInCompileOrderFromTasks"),),
        ),
        (
            "single-packet timing parameter",
            "bool submitPacket(GpuTimingSubmissionTicket* timingTicket);",
            ((1, "submitPacket/GpuTimingSubmissionTicket"),),
        ),
        (
            "single-packet hook call",
            "submitter.submitPacket(graph, static_cast<const QueueSubmissionPreSubmitHook*>(hook));",
            ((1, "submitPacket/QueueSubmissionPreSubmitHook"),),
        ),
        (
            "normal execution accepted callback",
            "struct GpuTaskGraphNormalExecutionDesc{\n    const void* acceptedCallback;\n};",
            ((2, "acceptedCallback"),),
        ),
        (
            "normal execution submission hooks",
            "struct GpuTaskGraphNormalExecutionDesc{\n    const void* submissionHooks;\n    usize submissionHookCount;\n};",
            ((2, "submissionHooks"), (3, "submissionHookCount")),
        ),
        (
            "final inherited normal execution body",
            "struct GpuTaskGraphNormalExecutionDesc final : ExecutionBase{\n"
            "    void validate(){ if(true){ return; } }\n"
            "    const void* acceptedCallback;\n"
            "};",
            ((3, "acceptedCallback"),),
        ),
        (
            "class normal execution body",
            "class GpuTaskGraphNormalExecutionDesc : public ExecutionBase{\n"
            "    const void* submissionHooks;\n"
            "};",
            ((2, "submissionHooks"),),
        ),
        (
            "nested and local generic names",
            "struct GpuTaskGraphNormalExecutionDesc final{\n"
            "    struct Nested{ const void* acceptedCallback; };\n"
            "    void validate(const void* submissionHooks){\n"
            "        usize submissionHookCount = 0u;\n"
            "    }\n"
            "};",
            (),
        ),
        (
            "task bindings",
            "struct GpuTaskGraphNormalExecutionDesc{\n"
            "    const GpuTaskGraphTaskTimingTicket* taskTimingTickets;\n"
            "    const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks;\n"
            "    const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks;\n"
            "};",
            (),
        ),
        (
            "generic names outside descriptor",
            "struct ExecutionState{ const void* acceptedCallback; const void* submissionHooks; usize submissionHookCount; };",
            (),
        ),
        (
            "low-level types outside single-packet shape",
            "GpuTimingSubmissionTicket* ticket; QueueSubmissionPreSubmitHook hook; submitPacketRange(ticket, hook);",
            (),
        ),
        (
            "comment",
            "// GpuTaskGraphPacketTimingTicket submitPacketRangeInCompileOrderFromTasks\n"
            "// submitPacket(GpuTimingSubmissionTicket*, QueueSubmissionPreSubmitHook*)",
            (),
        ),
        (
            "literal",
            'const char* text = "GpuTaskGraphPacketSubmissionHook submitTaskRangeInCompileOrderFromTasks '
            'submitPacket(QueueSubmissionPreSubmitHook*)";',
            (),
        ),
        (
            "near names",
            "GpuTaskGraphPacketTimingTicketList tickets; void submitPacketAlias(GpuTimingSubmissionTicket* ticket);\n"
            "void submitPacketRangeInCompileOrderFromTasksAgain(); usize acceptedCallbackCount;",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_packet_anchored_submission_references(source))
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
        for line, identifier in find_packet_anchored_submission_references(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired packet-anchored submission binding '{identifier}'"
            )

    if violations:
        print(
            "Task-graph timing, acceptance callbacks, and submission hooks must bind through semantic tasks, not compiler packets.",
            file=sys.stderr,
        )
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
