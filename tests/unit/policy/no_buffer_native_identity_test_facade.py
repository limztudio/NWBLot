#!/usr/bin/env python3
"""Keep retired buffer native-identity mutation facades out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(
    r"\b(?:revokeBufferNativeIdentityForTesting|restoreBufferNativeIdentityForTesting)\b"
)


def find_buffer_native_identity_test_facade_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_buffer_native_identity_test_facade_references,
            files_for=production_source_files,
            error_header="Production buffer identity must not expose retired test-only registry mutation.",
            violation_format="{path}:{line}: retired buffer native-identity test facade '{identifier}'",
            self_test_cases=(
                (
                    "revoke call",
                    "device.revokeBufferNativeIdentityForTesting(buffer, nativeBuffer);",
                    ((1, "revokeBufferNativeIdentityForTesting"),),
                ),
                (
                    "restore declaration",
                    "bool restoreBufferNativeIdentityForTesting(Buffer* buffer, Object nativeBuffer);",
                    ((1, "restoreBufferNativeIdentityForTesting"),),
                ),
                (
                    "multiple references",
                    "device.revokeBufferNativeIdentityForTesting(buffer, nativeBuffer);\n"
                    "device.restoreBufferNativeIdentityForTesting(buffer, nativeBuffer);\n"
                    "device.revokeBufferNativeIdentityForTesting(buffer, nativeBuffer);",
                    (
                        (1, "revokeBufferNativeIdentityForTesting"),
                        (2, "restoreBufferNativeIdentityForTesting"),
                        (3, "revokeBufferNativeIdentityForTesting"),
                    ),
                ),
                (
                    "comments",
                    "// device.revokeBufferNativeIdentityForTesting(buffer, nativeBuffer);\n"
                    "/* device.restoreBufferNativeIdentityForTesting(buffer, nativeBuffer); */",
                    (),
                ),
                (
                    "literals",
                    'const char* text = "revokeBufferNativeIdentityForTesting";\n'
                    'const char* raw = R"tag(restoreBufferNativeIdentityForTesting)tag";',
                    (),
                ),
                (
                    "near names",
                    "void revokeBufferNativeIdentityForTestingAgain();\n"
                    "void restoreBufferNativeIdentityForTestings();\n"
                    "void revokeBufferNativeIdentityForTest();",
                    (),
                ),
            ),
        )
    )
