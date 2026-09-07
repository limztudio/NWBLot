#!/usr/bin/env python3
"""Keep first-party C++ exception handling in the shared terminal entry boundary."""

from __future__ import annotations

import re
from pathlib import Path

from policy_scan import find_regex_matches, production_source_files, run_policy

TERMINAL_ENTRY_PATH = "core/common/terminal_entry.h"
CATCH_KEYWORD = re.compile(r"\bcatch\b")


def find_local_exception_handlers(source: str) -> list[tuple[int, str]]:
    return find_regex_matches(source, CATCH_KEYWORD)


def files_outside_terminal_entry(source_root: Path) -> list[Path]:
    return [
        path for path in production_source_files(source_root)
        if path.relative_to(source_root).as_posix() != TERMINAL_ENTRY_PATH
    ]


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_local_exception_handlers,
            files_for=files_outside_terminal_entry,
            error_header="C++ exception handlers belong only in the shared terminal entry boundary.",
            violation_format="{path}:{line}: local exception handler '{identifier}'",
            self_test_cases=(
                ("typed recovery", "try { work(); } catch(const Failure&) { return false; }", ((1, "catch"),)),
                ("rollback and rethrow", "try { work(); }\ncatch(...) { rollback(); throw; }", ((2, "catch"),)),
                ("function try block", "Owner::Owner() try : value(make()) {} catch(...) { throw; }", ((1, "catch"),)),
                ("macro handler", "#define LOCAL_HANDLER catch", ((1, "catch"),)),
                ("multiple handlers", "catch(const Failure&) {}\ncatch(...) {}", ((1, "catch"), (2, "catch"))),
                ("cleanup only", "ScopeExit cleanup([&]()noexcept{ release(); }); work();", ()),
                ("comments", "// catch(...) {}\n/* catch(const Failure&) {} */", ()),
                ("literals", 'const char* text = "catch(...)"; const char* raw = R"x(catch)x";', ()),
                ("near names", "void catcher(); bool catchPending = false;", ()),
            ),
        )
    )
