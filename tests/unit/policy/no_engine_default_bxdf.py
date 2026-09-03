#!/usr/bin/env python3
"""Reject engine-owned fallback BXDF policy."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from policy_scan import REPOSITORY_ROOT, find_regex_matches, run_self_test


ENGINE_SHADER_SUFFIXES = frozenset((".slang", ".slangi", ".surface", ".bxdf"))
FORBIDDEN_DEFAULT_HELPER = re.compile(r"\b(?:nwbSceneApplyLighting|nwbSceneShadeLight|nwbSceneTonemap)\b")


def find_forbidden_default_helpers(source: str) -> list[tuple[int, str]]:
    return find_regex_matches(source, FORBIDDEN_DEFAULT_HELPER)


def engine_shader_files(source_root: Path) -> list[Path]:
    engine_root = source_root / "impl" / "assets"
    return sorted(
        path
        for path in engine_root.rglob("*")
        if path.is_file() and path.suffix in ENGINE_SHADER_SUFFIXES
    )


def project_bxdf_files(source_root: Path) -> list[Path]:
    roots = (source_root / "CoolStuff", source_root / "tests")
    return sorted(path for root in roots for path in root.rglob("*.bxdf") if path.is_file())


def append_violations(paths: list[Path], source_root: Path, owner: str, out_violations: list[str]) -> None:
    for path in paths:
        source = path.read_text(encoding="utf-8", errors="replace")
        for line, helper in find_forbidden_default_helpers(source):
            out_violations.append(f"{path.relative_to(source_root)}:{line}: {owner} references retired default BXDF helper '{helper}'")


def local_self_test() -> int:
    return run_self_test(
        find_forbidden_default_helpers,
        (
            ("engine default", "half3 nwbSceneApplyLighting(){ return half3(0.0h); }", ((1, "nwbSceneApplyLighting"),)),
            ("engine BRDF lobe", "half3 nwbSceneShadeLight(){ return half3(0.0h); }", ((1, "nwbSceneShadeLight"),)),
            ("engine tonemap", "half3 nwbSceneTonemap(){ return half3(0.0h); }", ((1, "nwbSceneTonemap"),)),
            ("transport helper", "half3 nwbSceneResolveLight(){ return half3(0.0h); }", ()),
            ("comment", "// nwbSceneApplyLighting()", ()),
            ("literal", 'const char* text = "nwbSceneApplyLighting";', ()),
        ),
    )


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        return local_self_test()

    source_root = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else REPOSITORY_ROOT
    violations: list[str] = []
    append_violations(engine_shader_files(source_root), source_root, "engine shader asset", violations)
    append_violations(project_bxdf_files(source_root), source_root, "project BXDF", violations)
    if violations:
        print("The engine provides material/BXDF contracts and transport only; each project-owned .bxdf supplies its own policy.", file=sys.stderr)
        print("\n".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
