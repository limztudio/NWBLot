#!/usr/bin/env python3
"""Keep retired texture native-identity test facades out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(
    r"\b(?:revokeUnmanagedNativeTextureForTesting|releaseRevokedNativeTextureIdentityForTesting)\b"
)


def find_texture_native_identity_test_facade_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_texture_native_identity_test_facade_references,
            files_for=production_source_files,
            error_header="Production texture identity must not expose retired test-only lifecycle controls.",
            violation_format="{path}:{line}: retired texture native-identity test facade '{identifier}'",
            self_test_cases=(
                (
                    "revoke call",
                    "device.revokeUnmanagedNativeTextureForTesting(texture, nativeImage);",
                    ((1, "revokeUnmanagedNativeTextureForTesting"),),
                ),
                (
                    "release declaration",
                    "void releaseRevokedNativeTextureIdentityForTesting(Texture* texture, Object nativeImage);",
                    ((1, "releaseRevokedNativeTextureIdentityForTesting"),),
                ),
                (
                    "multiple references",
                    "device.revokeUnmanagedNativeTextureForTesting(texture, nativeImage);\n"
                    "device.releaseRevokedNativeTextureIdentityForTesting(texture, nativeImage);\n"
                    "device.revokeUnmanagedNativeTextureForTesting(texture, nativeImage);",
                    (
                        (1, "revokeUnmanagedNativeTextureForTesting"),
                        (2, "releaseRevokedNativeTextureIdentityForTesting"),
                        (3, "revokeUnmanagedNativeTextureForTesting"),
                    ),
                ),
                (
                    "comments",
                    "// device.revokeUnmanagedNativeTextureForTesting(texture, nativeImage);\n"
                    "/* device.releaseRevokedNativeTextureIdentityForTesting(texture, nativeImage); */",
                    (),
                ),
                (
                    "literals",
                    'const char* text = "revokeUnmanagedNativeTextureForTesting";\n'
                    'const char* raw = R"tag(releaseRevokedNativeTextureIdentityForTesting)tag";',
                    (),
                ),
                (
                    "near names",
                    "void revokeUnmanagedNativeTextureForTestingAgain();\n"
                    "void releaseRevokedNativeTextureIdentityForTestings();\n"
                    "void revokeUnmanagedNativeTextureForTest();",
                    (),
                ),
            ),
        )
    )
