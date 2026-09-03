#!/usr/bin/env python3
"""Keep recorded final-state lookup anchored to semantic graph tasks."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, first_party_source_files, run_policy


RETIRED_IDENTIFIER = re.compile(r"\bpacketFinalStateSeed\b")


def find_packet_final_state_queries(source: str):
    return find_regex_matches(source, RETIRED_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_packet_final_state_queries,
            files_for=first_party_source_files,
            error_header="Recorded final state must be resolved through semantic task IDs.",
            violation_format="{path}:{line}: retired packet final-state query '{identifier}'",
            self_test_cases=(
                (
                    "packet query call",
                    "recordedGraph.packetFinalStateSeed(packet);",
                    ((1, "packetFinalStateSeed"),),
                ),
                (
                    "packet query declaration",
                    "const State* packetFinalStateSeed(Packet packet)const;",
                    ((1, "packetFinalStateSeed"),),
                ),
                (
                    "inherited packet query",
                    "using Base::packetFinalStateSeed;",
                    ((1, "packetFinalStateSeed"),),
                ),
                (
                    "semantic task query",
                    "recordedGraph.taskFinalStateSeed(compiledGraph, task);",
                    (),
                ),
                (
                    "near names",
                    "void packetFinalStateSeeds(); void packetFinalStateSeedForTask();",
                    (),
                ),
                (
                    "comments and literals",
                    "// packetFinalStateSeed(packet);\n"
                    'const char* text = "packetFinalStateSeed";\n'
                    'const char* raw = R"tag(packetFinalStateSeed)tag";',
                    (),
                ),
            ),
        )
    )
