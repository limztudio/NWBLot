#!/usr/bin/env python3
"""Keep retired upload-pool diagnostic seams out of production C++ sources."""

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
    r"\b(?:BuildScratchPoolDiagnosticPeer|UploadPoolDiagnosticPeer|testingIdentity|"
    r"m_nextChunkIdentityForTesting|m_chunkCreationCountForTesting|m_poolReuseCountForTesting|"
    r"m_suballocationCountForTesting|m_lastChunkIdentityForTesting|m_lastSuballocationOffsetForTesting)\b"
)


def find_upload_pool_diagnostic_references(source: str) -> list[tuple[int, str]]:
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
        ("scratch peer", "friend class BuildScratchPoolDiagnosticPeer;", ((1, "BuildScratchPoolDiagnosticPeer"),)),
        ("upload peer", "friend class UploadPoolDiagnosticPeer;", ((1, "UploadPoolDiagnosticPeer"),)),
        ("chunk identity", "u64 testingIdentity = 0u;", ((1, "testingIdentity"),)),
        ("next identity", "u64 m_nextChunkIdentityForTesting = 0u;", ((1, "m_nextChunkIdentityForTesting"),)),
        ("creation counter", "++m_chunkCreationCountForTesting;", ((1, "m_chunkCreationCountForTesting"),)),
        ("reuse counter", "++m_poolReuseCountForTesting;", ((1, "m_poolReuseCountForTesting"),)),
        ("suballocation counter", "++m_suballocationCountForTesting;", ((1, "m_suballocationCountForTesting"),)),
        ("last identity", "m_lastChunkIdentityForTesting = chunk.testingIdentity;", ((1, "m_lastChunkIdentityForTesting"), (1, "testingIdentity"))),
        ("last offset", "m_lastSuballocationOffsetForTesting = offset;", ((1, "m_lastSuballocationOffsetForTesting"),)),
        ("comment", "// friend class UploadPoolDiagnosticPeer;", ()),
        ("literal", 'const char* text = "testingIdentity m_poolReuseCountForTesting";', ()),
        ("near names", "void UploadPoolDiagnosticPeerState(); u64 testingIdentityIndex = 0u;", ()),
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_upload_pool_diagnostic_references(source))
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
        for line, identifier in find_upload_pool_diagnostic_references(source):
            violations.append(f"{path.relative_to(source_root)}:{line}: retired upload-pool diagnostic seam '{identifier}'")

    if violations:
        print("Upload and build-scratch recycling must not depend on production test diagnostics.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
