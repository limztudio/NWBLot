#!/usr/bin/env python3
"""Keep translation-unit-local symbols out of shared headers."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, header_files, run_policy


HIDDEN_SYMBOL = re.compile(r"\b(__hidden_[A-Za-z0-9_]+)\b")


def find_hidden_symbols(source: str):
    return find_regex_matches(source, HIDDEN_SYMBOL)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_hidden_symbols,
            files_for=header_files,
            error_header="Use a named detail namespace for helpers shared through a header; reserve __hidden_* namespaces for one .cpp.",
            violation_format="{path}:{line}: header references translation-unit-local symbol '{identifier}'",
            self_test_cases=(
                ("hidden namespace", "namespace __hidden_parser{", ((1, "__hidden_parser"),)),
                ("hidden reference", "using Parser = __hidden_parser::Parser;", ((1, "__hidden_parser"),)),
                ("detail namespace", "namespace ParserDetail{", ()),
                ("comment", "// namespace __hidden_parser{", ()),
                ("literal", 'const char* text = "namespace __hidden_parser{";', ()),
            ),
        )
    )
