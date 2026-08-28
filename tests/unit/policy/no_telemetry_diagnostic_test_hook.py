#!/usr/bin/env python3
"""Keep the retired telemetry diagnostic test hook out of production C++ sources."""

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
    r"\b(?:DiagnosticCaptureTestHookStage|DiagnosticCaptureTestHook|SetDiagnosticCaptureTestHook|"
    r"g_CaptureTestHook|AfterGuardLoad|WaitingForActiveCallback)\b"
)


def find_telemetry_diagnostic_test_hook_references(source: str) -> list[tuple[int, str]]:
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
    )
    failed = False
    for name, source, expected in cases:
        actual = tuple(find_telemetry_diagnostic_test_hook_references(source))
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
        for line, identifier in find_telemetry_diagnostic_test_hook_references(source):
            violations.append(
                f"{path.relative_to(source_root)}:{line}: retired telemetry diagnostic test hook '{identifier}'"
            )

    if violations:
        print("Production telemetry must not depend on the retired diagnostic test hook.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
