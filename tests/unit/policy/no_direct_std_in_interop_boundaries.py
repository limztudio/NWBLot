#!/usr/bin/env python3
"""Reject direct standard-library containers in designated interop boundaries."""

from __future__ import annotations

import re
from pathlib import Path

from policy_scan import find_regex_matches, run_policy


INTEROP_BOUNDARIES = (
    "loader/main.cpp",
    "logger/server/crash_symbolicate_rgd.cpp",
    "resource_cooker/command_line.cpp",
    "utilities/fbx_to_nwb/command_line.cpp",
    "utilities/tex_conv/command_line.cpp",
)
DIRECT_INTEROP_CONTAINER = re.compile(r"\bstd\s*::\s*(?:string|vector)\b")


def find_direct_interop_containers(source: str):
    return find_regex_matches(source, DIRECT_INTEROP_CONTAINER)


def interop_files(source_root: Path):
    return [source_root / relative_path for relative_path in INTEROP_BOUNDARIES]


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_direct_interop_containers,
            files_for=interop_files,
            error_header="Interop boundaries must use project-owned aliases instead of direct std::string/std::vector names:",
            violation_format="{path}:{line}: direct std::string/std::vector reference",
            self_test_cases=(
                ("direct string", "std::string value;", ((1, "std::string"),)),
                ("direct vector", "std :: vector<int> values;", ((1, "std :: vector"),)),
                ("comment", "// std::string value;", ()),
                ("literal", 'const char* text = "std::vector";', ()),
            ),
        )
    )
