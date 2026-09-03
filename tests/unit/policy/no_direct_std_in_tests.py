#!/usr/bin/env python3
"""Reject direct C++ standard-library references in project-owned test sources."""

from __future__ import annotations

import re
from pathlib import Path

from policy_scan import SOURCE_SUFFIXES, files_under, find_regex_matches, run_policy


DIRECT_STD = re.compile(r"\bstd\s*::")


def find_direct_std_references(source: str):
    return find_regex_matches(source, DIRECT_STD)


def source_files(source_root: Path):
    return files_under(source_root, ("tests",), SOURCE_SUFFIXES)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_direct_std_references,
            files_for=source_files,
            error_header="Project-owned C++ test sources must use global features instead of direct std:: names:",
            violation_format="{path}:{line}: direct std:: reference",
            self_test_cases=(
                ("direct reference", "std::vector<int> values;", ((1, "std::"),)),
                ("spaced reference", "std  :: vector<int> values;", ((1, "std  ::"),)),
                ("comment", "// std::vector<int> values;", ()),
                ("literal", 'const char* text = "std::vector";', ()),
            ),
        )
    )
