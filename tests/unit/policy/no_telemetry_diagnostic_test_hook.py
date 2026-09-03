#!/usr/bin/env python3
"""Keep the retired telemetry diagnostic test hook out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(
    r"\b(?:DiagnosticCaptureTestHookStage|DiagnosticCaptureTestHook|SetDiagnosticCaptureTestHook|"
    r"g_CaptureTestHook|AfterGuardLoad|WaitingForActiveCallback)\b"
)


def find_telemetry_diagnostic_test_hook_references(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_telemetry_diagnostic_test_hook_references,
            files_for=production_source_files,
            error_header="Production telemetry must not depend on the retired diagnostic test hook.",
            violation_format="{path}:{line}: retired telemetry diagnostic test hook '{identifier}'",
            self_test_cases=(
                (
                    "hook stage type",
                    "namespace DiagnosticCaptureTestHookStage{ enum Enum : u8{}; };",
                    ((1, "DiagnosticCaptureTestHookStage"),),
                ),
                (
                    "hook alias",
                    "using DiagnosticCaptureTestHook = void (*)(u8);",
                    ((1, "DiagnosticCaptureTestHook"),),
                ),
                (
                    "hook setter",
                    "SetDiagnosticCaptureTestHook(callback);",
                    ((1, "SetDiagnosticCaptureTestHook"),),
                ),
                (
                    "hook storage",
                    "Atomic<DiagnosticCaptureTestHook> g_CaptureTestHook;",
                    (
                        (1, "DiagnosticCaptureTestHook"),
                        (1, "g_CaptureTestHook"),
                    ),
                ),
                (
                    "hook stages",
                    "testHook(AfterGuardLoad);\ntestHook(WaitingForActiveCallback);",
                    (
                        (1, "AfterGuardLoad"),
                        (2, "WaitingForActiveCallback"),
                    ),
                ),
                (
                    "multiple hook references",
                    "DiagnosticCaptureTestHook hook = g_CaptureTestHook.load();\n"
                    "SetDiagnosticCaptureTestHook(hook);\n"
                    "switch(DiagnosticCaptureTestHookStage::AfterGuardLoad){}",
                    (
                        (1, "DiagnosticCaptureTestHook"),
                        (1, "g_CaptureTestHook"),
                        (2, "SetDiagnosticCaptureTestHook"),
                        (3, "DiagnosticCaptureTestHookStage"),
                        (3, "AfterGuardLoad"),
                    ),
                ),
                (
                    "comments",
                    "// SetDiagnosticCaptureTestHook(hook);\n"
                    "/* DiagnosticCaptureTestHookStage::AfterGuardLoad;\n"
                    "g_CaptureTestHook = nullptr; */",
                    (),
                ),
                (
                    "literals",
                    'const char* text = "DiagnosticCaptureTestHook WaitingForActiveCallback";\n'
                    'const char* raw = R"tag(SetDiagnosticCaptureTestHook g_CaptureTestHook)tag";',
                    (),
                ),
                (
                    "near names",
                    "struct DiagnosticCaptureTestHookStageState;\n"
                    "using DiagnosticCaptureTestHooks = void(*)();\n"
                    "void SetDiagnosticCaptureTestHookAgain();\n"
                    "void g_CaptureTestHookValue();\n"
                    "void AfterGuardLoaded();\n"
                    "void WaitingForActiveCallbacks();",
                    (),
                ),
            ),
        )
    )
