#!/usr/bin/env python3
"""Keep retired upload-pool diagnostic seams out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(
    r"\b(?:BuildScratchPoolDiagnosticPeer|UploadPoolDiagnosticPeer|testingIdentity|"
    r"m_nextChunkIdentityForTesting|m_chunkCreationCountForTesting|m_poolReuseCountForTesting|"
    r"m_suballocationCountForTesting|m_lastChunkIdentityForTesting|m_lastSuballocationOffsetForTesting)\b"
)


def find_upload_pool_diagnostic_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_upload_pool_diagnostic_references,
            files_for=production_source_files,
            error_header="Upload and build-scratch recycling must not depend on production test diagnostics.",
            violation_format="{path}:{line}: retired upload-pool diagnostic seam '{identifier}'",
            self_test_cases=(
                ("scratch peer", "friend class BuildScratchPoolDiagnosticPeer;", ((1, "BuildScratchPoolDiagnosticPeer"),)),
                ("upload peer", "friend class UploadPoolDiagnosticPeer;", ((1, "UploadPoolDiagnosticPeer"),)),
                ("chunk identity", "u64 testingIdentity = 0u;", ((1, "testingIdentity"),)),
                ("next identity", "u64 m_nextChunkIdentityForTesting = 0u;", ((1, "m_nextChunkIdentityForTesting"),)),
                ("creation counter", "++m_chunkCreationCountForTesting;", ((1, "m_chunkCreationCountForTesting"),)),
                ("reuse counter", "++m_poolReuseCountForTesting;", ((1, "m_poolReuseCountForTesting"),)),
                ("suballocation counter", "++m_suballocationCountForTesting;", ((1, "m_suballocationCountForTesting"),)),
                (
                    "last identity",
                    "m_lastChunkIdentityForTesting = chunk.testingIdentity;",
                    ((1, "m_lastChunkIdentityForTesting"), (1, "testingIdentity")),
                ),
                ("last offset", "m_lastSuballocationOffsetForTesting = offset;", ((1, "m_lastSuballocationOffsetForTesting"),)),
                ("comment", "// friend class UploadPoolDiagnosticPeer;", ()),
                ("literal", 'const char* text = "testingIdentity m_poolReuseCountForTesting";', ()),
                ("near names", "void UploadPoolDiagnosticPeerState(); u64 testingIdentityIndex = 0u;", ()),
            ),
        )
    )
