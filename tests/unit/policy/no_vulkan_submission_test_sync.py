#!/usr/bin/env python3
"""Keep retired Vulkan submission synchronization and observation test seams out of production."""

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
    r"\b(?:createSubmissionSignalForTesting|destroySubmissionSignalForTesting|"
    r"createSubmissionTimelineForTesting|signalSubmissionTimelineForTesting|"
    r"destroySubmissionTimelineForTesting|EncodeSubmissionNativeSemaphore|"
    r"clearSubmissionWaitTokensForTesting|armSubmissionWaitCaptureForTesting|"
    r"lastSubmissionWaitTokenCountForTesting|lastSubmissionWaitTokenForTesting|"
    r"captureSubmissionWaitTokensForTesting|m_submissionWaitCaptureArmedForTesting|"
    r"m_submissionWaitTokensForTestingMutex|m_submissionWaitQueueForTesting|"
    r"m_submissionWaitTokensForTesting|rejectNextSubmissionForTesting|"
    r"clearSubmissionRejectionsForTesting|consumeSubmissionRejectionForTesting|"
    r"m_submissionRejectionsForTesting)\b"
)


def find_vulkan_submission_test_sync_references(source: str) -> list[tuple[int, str]]:
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
        ("binary creation", "device.createSubmissionSignalForTesting(signal);", ((1, "createSubmissionSignalForTesting"),)),
        ("binary destruction", "device.destroySubmissionSignalForTesting(signal);", ((1, "destroySubmissionSignalForTesting"),)),
        ("timeline creation", "device.createSubmissionTimelineForTesting(wait);", ((1, "createSubmissionTimelineForTesting"),)),
        ("timeline signal", "device.signalSubmissionTimelineForTesting(wait);", ((1, "signalSubmissionTimelineForTesting"),)),
        ("timeline destruction", "device.destroySubmissionTimelineForTesting(wait);", ((1, "destroySubmissionTimelineForTesting"),)),
        ("native encoding", "Object EncodeSubmissionNativeSemaphore(VkSemaphore semaphore);", ((1, "EncodeSubmissionNativeSemaphore"),)),
        ("wait capture clear", "device.clearSubmissionWaitTokensForTesting();", ((1, "clearSubmissionWaitTokensForTesting"),)),
        ("wait capture arm", "device.armSubmissionWaitCaptureForTesting();", ((1, "armSubmissionWaitCaptureForTesting"),)),
        (
            "wait token count",
            "device.lastSubmissionWaitTokenCountForTesting(queue);",
            ((1, "lastSubmissionWaitTokenCountForTesting"),),
        ),
        (
            "wait token lookup",
            "device.lastSubmissionWaitTokenForTesting(queue, 0u);",
            ((1, "lastSubmissionWaitTokenForTesting"),),
        ),
        (
            "wait token capture",
            "captureSubmissionWaitTokensForTesting(queue, waits, waitCount);",
            ((1, "captureSubmissionWaitTokensForTesting"),),
        ),
        (
            "wait capture armed state",
            "Atomic<bool> m_submissionWaitCaptureArmedForTesting = false;",
            ((1, "m_submissionWaitCaptureArmedForTesting"),),
        ),
        (
            "wait token mutex",
            "Futex m_submissionWaitTokensForTestingMutex;",
            ((1, "m_submissionWaitTokensForTestingMutex"),),
        ),
        (
            "wait queue state",
            "GpuPhysicalQueueId m_submissionWaitQueueForTesting;",
            ((1, "m_submissionWaitQueueForTesting"),),
        ),
        (
            "wait token state",
            "GraphicsVector<QueueSubmissionToken> m_submissionWaitTokensForTesting;",
            ((1, "m_submissionWaitTokensForTesting"),),
        ),
        (
            "multiple wait capture references",
            "device.armSubmissionWaitCaptureForTesting();\n"
            "device.lastSubmissionWaitTokenForTesting(queue, 0u);\n"
            "device.armSubmissionWaitCaptureForTesting();",
            (
                (1, "armSubmissionWaitCaptureForTesting"),
                (2, "lastSubmissionWaitTokenForTesting"),
                (3, "armSubmissionWaitCaptureForTesting"),
            ),
        ),
        (
            "comments",
            "// device.clearSubmissionWaitTokensForTesting();\n"
            "/* device.armSubmissionWaitCaptureForTesting(); */",
            (),
        ),
        (
            "literals",
            'const char* text = "lastSubmissionWaitTokenForTesting";\n'
            'const char* raw = R"tag(m_submissionWaitTokensForTesting)tag";',
            (),
        ),
        (
            "near names",
            "void clearSubmissionWaitTokensForTestingAgain();\n"
            "void lastSubmissionWaitTokenForTestings();\n"
            "bool m_submissionWaitCaptureArmedForTestingState = false;\n"
            "void captureSubmissionWaitTokenForTesting();",
            (),
        ),
        (
            "submission rejection arm",
            "device.rejectNextSubmissionForTesting(CommandQueue::Graphics);",
            ((1, "rejectNextSubmissionForTesting"),),
        ),
        (
            "submission rejection clear",
            "device.clearSubmissionRejectionsForTesting();",
            ((1, "clearSubmissionRejectionsForTesting"),),
        ),
        (
            "submission rejection consume",
            "device.consumeSubmissionRejectionForTesting(CommandQueue::Graphics);",
            ((1, "consumeSubmissionRejectionForTesting"),),
        ),
        (
            "submission rejection state",
            "Atomic<u32> m_submissionRejectionsForTesting;",
            ((1, "m_submissionRejectionsForTesting"),),
        ),
        (
            "multiple submission rejection references",
            "device.rejectNextSubmissionForTesting(CommandQueue::Graphics);\n"
            "device.consumeSubmissionRejectionForTesting(CommandQueue::Graphics);\n"
            "device.rejectNextSubmissionForTesting(CommandQueue::Compute);\n"
            "device.clearSubmissionRejectionsForTesting();\n"
            "Atomic<u32> m_submissionRejectionsForTesting;",
            (
                (1, "rejectNextSubmissionForTesting"),
                (2, "consumeSubmissionRejectionForTesting"),
                (3, "rejectNextSubmissionForTesting"),
                (4, "clearSubmissionRejectionsForTesting"),
                (5, "m_submissionRejectionsForTesting"),
            ),
        ),
        (
            "submission rejection comments",
            "// device.rejectNextSubmissionForTesting(CommandQueue::Graphics);\n"
            "/* device.clearSubmissionRejectionsForTesting();\n"
            "device.consumeSubmissionRejectionForTesting(CommandQueue::Graphics);\n"
            "Atomic<u32> m_submissionRejectionsForTesting; */",
            (),
        ),
        (
            "submission rejection literals",
            'const char* text = "rejectNextSubmissionForTesting clearSubmissionRejectionsForTesting";\n'
            'const char* raw = R"tag(consumeSubmissionRejectionForTesting m_submissionRejectionsForTesting)tag";',
            (),
        ),
        (
            "submission rejection near names",
            "void rejectNextSubmissionForTestingAgain();\n"
            "void clearSubmissionRejectionsForTestings();\n"
            "void consumeSubmissionRejectionForTestingState();\n"
            "Atomic<u32> m_submissionRejectionsForTestingCount;",
            (),
        ),
        ("legacy comment", "// device.createSubmissionSignalForTesting(signal);", ()),
        ("legacy literal", 'const char* text = "destroySubmissionTimelineForTesting";', ()),
        ("legacy near name", "void createSubmissionSignalForTestingAgain();", ()),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_vulkan_submission_test_sync_references(source))
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
        for line, identifier in find_vulkan_submission_test_sync_references(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired Vulkan submission-sync test helper '{identifier}'"
            )

    if violations:
        print("Production Vulkan submission must not expose retired test-owned synchronization or observation seams.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
