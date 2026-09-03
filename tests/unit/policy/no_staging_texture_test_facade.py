#!/usr/bin/env python3
"""Keep retired staging-texture test seams out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(
    r"\b(?:hasMappedMemoryForTesting|isPersistentlyMappedForTesting|"
    r"rejectNextInvalidateForTesting|m_rejectNextInvalidateForTesting)\b"
)


def find_staging_texture_test_facade_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_staging_texture_test_facade_references,
            files_for=production_source_files,
            error_header="Production staging-texture behavior must not depend on retired test-only mapping controls.",
            violation_format="{path}:{line}: retired staging-texture test facade '{identifier}'",
            self_test_cases=(
                ("mapped-memory query", "staging.hasMappedMemoryForTesting();", ((1, "hasMappedMemoryForTesting"),)),
                (
                    "persistent-mapping query",
                    "staging.isPersistentlyMappedForTesting();",
                    ((1, "isPersistentlyMappedForTesting"),),
                ),
                (
                    "invalidate rejection command",
                    "staging.rejectNextInvalidateForTesting();",
                    ((1, "rejectNextInvalidateForTesting"),),
                ),
                (
                    "invalidate rejection state",
                    "bool m_rejectNextInvalidateForTesting = false;",
                    ((1, "m_rejectNextInvalidateForTesting"),),
                ),
                (
                    "multiple identifiers",
                    "if(staging.hasMappedMemoryForTesting())\n"
                    "    staging.rejectNextInvalidateForTesting();",
                    (
                        (1, "hasMappedMemoryForTesting"),
                        (2, "rejectNextInvalidateForTesting"),
                    ),
                ),
                (
                    "comments",
                    "// staging.hasMappedMemoryForTesting();\n"
                    "/* staging.rejectNextInvalidateForTesting(); */",
                    (),
                ),
                (
                    "literals",
                    'const char* text = "isPersistentlyMappedForTesting m_rejectNextInvalidateForTesting";\n'
                    'const char* raw = R"tag(rejectNextInvalidateForTesting)tag";',
                    (),
                ),
                (
                    "near names",
                    "bool hasMappedMemoryForTestingAgain();\n"
                    "bool isPersistentlyMappedForTestings();\n"
                    "void rejectNextInvalidateForTestingLater();\n"
                    "bool m_rejectNextInvalidateForTestingState = false;",
                    (),
                ),
            ),
        )
    )
