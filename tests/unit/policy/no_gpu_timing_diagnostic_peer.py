#!/usr/bin/env python3
"""Keep the retired GPU timing diagnostic peer out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(r"\bGpuTimingRecorderDiagnosticPeer\b")


def find_gpu_timing_diagnostic_peer_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_gpu_timing_diagnostic_peer_references,
            files_for=production_source_files,
            error_header="Production GPU timing must not expose the retired test-only diagnostic peer.",
            violation_format="{path}:{line}: retired GPU timing diagnostic peer '{identifier}'",
            self_test_cases=(
                (
                    "friend declaration",
                    "friend class GpuTimingRecorderDiagnosticPeer;",
                    ((1, "GpuTimingRecorderDiagnosticPeer"),),
                ),
                (
                    "multiple references",
                    "class GpuTimingRecorderDiagnosticPeer;\n"
                    "friend class GpuTimingRecorderDiagnosticPeer;\n"
                    "GpuTimingRecorderDiagnosticPeer::dispatchSample(recorder, sample);",
                    (
                        (1, "GpuTimingRecorderDiagnosticPeer"),
                        (2, "GpuTimingRecorderDiagnosticPeer"),
                        (3, "GpuTimingRecorderDiagnosticPeer"),
                    ),
                ),
                (
                    "comments",
                    "// friend class GpuTimingRecorderDiagnosticPeer;\n"
                    "/* class GpuTimingRecorderDiagnosticPeer; */",
                    (),
                ),
                (
                    "literals",
                    'const char* text = "GpuTimingRecorderDiagnosticPeer";\n'
                    'const char* raw = R"tag(GpuTimingRecorderDiagnosticPeer)tag";',
                    (),
                ),
                (
                    "near names",
                    "class GpuTimingRecorderDiagnosticPeerState;\n"
                    "class AlternateGpuTimingRecorderDiagnosticPeer;",
                    (),
                ),
            ),
        )
    )
