#!/usr/bin/env python3
"""Keep the retired RenderLane facade out of production C++ sources."""

from __future__ import annotations

import re

from policy_scan import find_regex_matches, production_source_files, run_policy


RETIRED_FACADE_IDENTIFIER = re.compile(
    r"\b(?:RenderLane|renderLane|setRenderLane|resolveRenderLane|isRenderLaneDedicated)\b"
)


def find_render_lane_references(source: str):
    return find_regex_matches(source, RETIRED_FACADE_IDENTIFIER)


if __name__ == "__main__":
    raise SystemExit(
        run_policy(
            finder=find_render_lane_references,
            files_for=production_source_files,
            error_header="Production C++ must use physical CommandQueue identities instead of the retired RenderLane facade.",
            violation_format="{path}:{line}: retired RenderLane facade identifier '{identifier}'",
            self_test_cases=(
                ("namespace", "namespace RenderLane{ enum Enum{}; };", ((1, "RenderLane"),)),
                ("field", "CommandQueue::Enum renderLane = CommandQueue::Graphics;", ((1, "renderLane"),)),
                ("setter", "parameters.setRenderLane(CommandQueue::Compute);", ((1, "setRenderLane"),)),
                ("resolver", "device.resolveRenderLane(lane);", ((1, "resolveRenderLane"),)),
                ("dedicated query", "device.isRenderLaneDedicated(lane);", ((1, "isRenderLaneDedicated"),)),
                ("comment", "// RenderLane::Graphics", ()),
                ("block comment", "/* setRenderLane(RenderLane::AsyncCompute) */", ()),
                ("literal", 'const char* text = "resolveRenderLane";', ()),
                (
                    "similarly named identifiers",
                    "struct RenderLanePolicy{ int renderLaneIndex; void setRenderLanePolicy(); bool isRenderLaneDedicatedQueue(); };",
                    (),
                ),
            ),
        )
    )
