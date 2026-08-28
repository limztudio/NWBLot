#!/usr/bin/env python3
"""Keep retired manual task-graph lifecycle and packet-transaction seams out of production C++ sources."""

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
    r"\b(?:GpuGraphSubmissionTransactionDiagnosticPeer|discardTask|markPacketRecorded|acceptPacket|packetRuntime|"
    r"latestAcceptedToken|submissionWaiterCountForTesting|m_submissionWaiterCount|hasNativeSubmission)\b"
)
PACKET_RUNTIME_STATE_SCOPE = re.compile(
    r"\b(?:namespace\s+GpuPacketRuntimeState|(?:class|struct)\s+PacketRuntimeState|"
    r"enum\s+class\s+PacketRuntimeState(?:\s*:\s*[^;{}]+)?)\s*\{(?P<body>.*?)\};",
    re.DOTALL,
)
RECORDED_IDENTIFIER = re.compile(r"\bRecorded\b")


def find_manual_transaction_references(source: str) -> list[tuple[int, str]]:
    code = blank_non_code(source)
    references = [
        (line_number(code, match.start()), match.group())
        for match in RETIRED_IDENTIFIER.finditer(code)
    ]
    for state_scope_match in PACKET_RUNTIME_STATE_SCOPE.finditer(code):
        body_start = state_scope_match.start("body")
        for recorded_match in RECORDED_IDENTIFIER.finditer(state_scope_match.group("body")):
            references.append((line_number(code, body_start + recorded_match.start()), "PacketRuntimeState::Recorded"))
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
        ("diagnostic peer", "friend class GpuGraphSubmissionTransactionDiagnosticPeer;", ((1, "GpuGraphSubmissionTransactionDiagnosticPeer"),)),
        ("task discard seam", "graph.discardTask(task);", ((1, "discardTask"),)),
        ("task discard definition", "void GpuTaskGraph::discardTask(const GpuTaskId&){}", ((1, "discardTask"),)),
        ("record seam", "transaction.markPacketRecorded(graph, compiled, packet, attempt);", ((1, "markPacketRecorded"),)),
        ("accept seam", "transaction.acceptPacket(graph, compiled, packet, token);", ((1, "acceptPacket"),)),
        ("borrowed runtime", "transaction.packetRuntime(packet);", ((1, "packetRuntime"),)),
        ("borrowed frontier", "transaction.latestAcceptedToken(queue);", ((1, "latestAcceptedToken"),)),
        ("waiter counter", "transaction.submissionWaiterCountForTesting();", ((1, "submissionWaiterCountForTesting"),)),
        ("native marker", "runtime.hasNativeSubmission = true;", ((1, "hasNativeSubmission"),)),
        (
            "duplicate recorded state",
            "namespace GpuPacketRuntimeState{ enum Enum : u8{ Declared, Recorded, Accepted }; };",
            ((1, "PacketRuntimeState::Recorded"),),
        ),
        (
            "duplicate nested recorded state",
            "class GpuGraphSubmissionTransaction{ private: enum class PacketRuntimeState : u8{ Declared, Recorded, Accepted }; };",
            ((1, "PacketRuntimeState::Recorded"),),
        ),
        ("comment", "// graph.discardTask(task); transaction.acceptPacket(graph, compiled, packet, token);", ()),
        ("literal", 'const char* text = "discardTask markPacketRecorded packetRuntime";', ()),
        (
            "near names",
            "void acceptPacketBatch(); usize packetRuntimeIndex = 0u; "
            "void discardTasks(); void discardTaskRange(); usize discardTaskId = 0u;",
            (),
        ),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_manual_transaction_references(source))
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
        for line, identifier in find_manual_transaction_references(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: retired manual task-graph seam '{identifier}'")

    if violations:
        print("Task lifecycle must resolve through graph abandonment or native packet transactions, without production test seams.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
