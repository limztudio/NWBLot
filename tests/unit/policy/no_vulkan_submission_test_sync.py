#!/usr/bin/env python3
"""Keep retired Vulkan submission-sync test helpers out of the production backend."""

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
    r"destroySubmissionTimelineForTesting|EncodeSubmissionNativeSemaphore)\b"
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
        ("comment", "// device.createSubmissionSignalForTesting(signal);", ()),
        ("literal", 'const char* text = "destroySubmissionTimelineForTesting";', ()),
        ("near names", "void createSubmissionSignalForTestingAgain();", ()),
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
        print("Production Vulkan submission must not expose test-owned semaphore lifecycle helpers.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
