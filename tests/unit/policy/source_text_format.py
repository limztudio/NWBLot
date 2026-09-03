#!/usr/bin/env python3
"""Enforce the first-party source text format required by .helper/standard.md."""

from __future__ import annotations

import sys
from pathlib import Path


from policy_scan import FIRST_PARTY_CODE_DIRECTORIES, REPOSITORY_ROOT


SOURCE_DIRECTORIES = FIRST_PARTY_CODE_DIRECTORIES
CPP_SUFFIXES = frozenset((".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ixx"))
SHADER_SUFFIXES = frozenset((".slang", ".slangi", ".bind", ".surface", ".bxdf"))
METADATA_SUFFIXES = frozenset((".nwb",))
SEPARATOR = b"/" * 128
BANNER = b"// limztudio@gmail.com\r\n" + SEPARATOR + b"\r\n"
EOF_SUFFIX = SEPARATOR + b"\r\n\r\n"


def find_source_format_violations(
    source: bytes,
    requires_banner: bool,
    requires_eof_suffix: bool,
) -> list[str]:
    violations: list[str] = []
    try:
        source.decode("utf-8")
    except UnicodeDecodeError:
        violations.append("is not valid UTF-8")

    if source.startswith(b"\xef\xbb\xbf"):
        violations.append("has a UTF-8 BOM")

    if any(source[index - 1:index] != b"\r" for index, value in enumerate(source) if value == ord("\n")):
        violations.append("uses LF-only or mixed line endings")
    if any(source[index + 1:index + 2] != b"\n" for index, value in enumerate(source) if value == ord("\r")):
        violations.append("contains a CR not followed by LF")

    if requires_banner and not source.startswith(BANNER):
        violations.append("does not start with the required project banner")
    if requires_eof_suffix and not source.endswith(EOF_SUFFIX):
        violations.append("does not end with the required separator and exactly one blank line")
    return violations


def source_files(source_root: Path) -> list[tuple[Path, bool, bool]]:
    result: list[tuple[Path, bool, bool]] = []
    for relative_directory in SOURCE_DIRECTORIES:
        directory = source_root / relative_directory
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix in CPP_SUFFIXES or path.suffix in SHADER_SUFFIXES:
                result.append((path, True, True))
            elif path.suffix in METADATA_SUFFIXES:
                result.append((path, False, False))
    return sorted(result, key=lambda entry: entry[0])


def run_self_test() -> int:
    valid_source = BANNER + b"void f(){}\r\n" + EOF_SUFFIX
    cases = (
        ("valid source", valid_source, True, True, ()),
        ("LF source", valid_source.replace(b"\r\n", b"\n"), True, True, ("uses LF-only or mixed line endings", "does not start with the required project banner", "does not end with the required separator and exactly one blank line")),
        ("missing banner", b"void f(){}\r\n" + EOF_SUFFIX, True, True, ("does not start with the required project banner",)),
        ("missing EOF blank line", BANNER + b"void f(){}\r\n" + SEPARATOR + b"\r\n", True, True, ("does not end with the required separator and exactly one blank line",)),
        ("metadata", b"material asset;\r\n", False, False, ()),
        ("BOM", b"\xef\xbb\xbf" + valid_source, True, True, ("has a UTF-8 BOM", "does not start with the required project banner")),
    )
    failed = False
    for name, source, requires_banner, requires_eof_suffix, expected in cases:
        actual = tuple(find_source_format_violations(source, requires_banner, requires_eof_suffix))
        if actual != expected:
            print(f"{name}: expected {expected}, got {actual}", file=sys.stderr)
            failed = True
    return 1 if failed else 0


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return run_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    for path, requires_banner, requires_eof_suffix in source_files(source_root):
        for violation in find_source_format_violations(path.read_bytes(), requires_banner, requires_eof_suffix):
            violations.append(f"{path.relative_to(source_root)}: {violation}")

    if violations:
        print("First-party source text must use the project UTF-8, CRLF, banner, and EOF conventions:", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
