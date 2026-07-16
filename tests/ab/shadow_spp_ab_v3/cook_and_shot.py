#!/usr/bin/env python3
"""Cook one software-shadow SPP variant, then capture its screenshot."""

import re
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
BUILD = REPO / "__cmake/build/linux-clang-x64"
RUNTIME = BUILD / "Testing/skinning_culling_benchmark_runtime/opt"
HEADER = REPO / "impl/assets/graphics/shadow/sw_binding_slots.h"
COOK_TARGET = "nwb_skinning_culling_benchmark_assets"


def parse_spp(arguments):
    if len(arguments) != 3:
        raise SystemExit(f"usage: {arguments[0]} <spp> <output.bmp>")

    try:
        spp = int(arguments[1], 10)
    except ValueError as error:
        raise SystemExit(f"SPP must be a non-negative integer: {arguments[1]!r}") from error

    if spp < 0:
        raise SystemExit(f"SPP must be a non-negative integer: {arguments[1]!r}")
    return spp, Path(arguments[2]).resolve()


def cmake_binary():
    bundled_cmake = REPO / "__cmake/tool-venv/bin/cmake"
    return str(bundled_cmake) if bundled_cmake.is_file() else "cmake"


def set_shadow_spp(spp):
    source = HEADER.read_bytes()
    replacement = f"#define NWB_SW_SHADOW_SOFT_SPP {spp}u".encode("ascii")
    updated, replacements = re.subn(
        rb"(?m)^#define NWB_SW_SHADOW_SOFT_SPP [0-9]+u(?=\r?$)",
        replacement,
        source,
    )
    if replacements != 1:
        raise SystemExit(f"expected one NWB_SW_SHADOW_SOFT_SPP define in {HEADER}, found {replacements}")

    if updated != source:
        HEADER.write_bytes(updated)


def remove_previous_cook_outputs():
    resource_directory = RUNTIME / "res"
    outputs = [resource_directory / ".nwb_skinning_culling_benchmark_assets_opt.stamp"]
    outputs.extend(resource_directory.glob("*.vol"))
    for output in outputs:
        if output.exists():
            output.unlink()


def run(command, *, cwd, timeout):
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    if result.returncode == 0:
        return result

    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    raise SystemExit(f"command failed ({result.returncode}): {' '.join(command)}")


def main():
    spp, output_bmp = parse_spp(sys.argv)
    output_bmp.parent.mkdir(parents=True, exist_ok=True)

    set_shadow_spp(spp)
    print(f"set SPP={spp}")

    remove_previous_cook_outputs()
    run(
        [cmake_binary(), "--build", str(BUILD), "--config", "opt", "--target", COOK_TARGET],
        cwd=REPO,
        timeout=120,
    )

    volumes = sorted((RUNTIME / "res").glob("*.vol"))
    if not volumes:
        raise SystemExit(f"cook completed without a .vol output in {RUNTIME / 'res'}")
    print("cooked volumes:", ", ".join(f"{volume.name} ({volume.stat().st_size} bytes)" for volume in volumes))

    run(
        [sys.executable, str(REPO / "tests/ab/shadow_spp_ab_v3/screenshot.py"), str(output_bmp)],
        cwd=REPO,
        timeout=90,
    )
    if not output_bmp.is_file():
        raise SystemExit(f"screenshot completed without output: {output_bmp}")
    print(f"bmp {output_bmp} ({output_bmp.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
