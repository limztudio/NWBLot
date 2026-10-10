#!/usr/bin/env python3

import argparse
import pathlib
import subprocess
import tempfile

LIT_APPEND = "append"
LIT_HELP = "--help"
LIT_NWB_ASSET_PIPELINE = "NWB asset pipeline"
LIT_OUTPUT = "--output"
LIT_NWB_UNKNOWN_OPTION = "--nwb-unknown-option"
LIT_OUTPUT_2 = "output"
LIT_MAIN = "__main__"
LIT_UTF_8 = "utf-8"
LIT_STARTUP_MARKERS = ("Log server:", "Loader:", "GraphicsRuntime:", "Vulkan:")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pipeline", type=pathlib.Path, action=LIT_APPEND, default=[])
    parser.add_argument("--utility", type=pathlib.Path, action=LIT_APPEND, default=[])
    parser.add_argument("--application", type=pathlib.Path, action=LIT_APPEND, default=[])
    return parser.parse_args()


def run(executable: pathlib.Path, arguments: list[str], root: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(executable.resolve()), *arguments], cwd=root, capture_output=True, text=True,
        encoding=LIT_UTF_8, errors="replace", timeout=30.0, check=False,
    )


def verify_pipeline(executable: pathlib.Path, root: pathlib.Path) -> None:
    for arguments in ([], [LIT_NWB_UNKNOWN_OPTION], [LIT_OUTPUT, str(root / LIT_OUTPUT_2)]):
        result = run(executable, arguments, root)
        assert result.returncode == 1, (executable, arguments, result)
        if LIT_OUTPUT not in arguments:
            assert LIT_NWB_ASSET_PIPELINE in result.stderr and LIT_OUTPUT in result.stderr, result
            assert "failed to parse command line" in result.stdout + result.stderr, result
        else:
            assert "provide --input or --input-list" in result.stdout + result.stderr, result
        assert not (root / LIT_OUTPUT_2).exists(), (executable, arguments, "created output after failure")


def verify_utility(executable: pathlib.Path, root: pathlib.Path) -> None:
    result = run(executable, [LIT_NWB_UNKNOWN_OPTION], root)
    # CLI11's ExtrasError is 109; preserve its native terminal code instead of normalizing it to generic failure.
    assert result.returncode == 109, (executable, result)
    assert LIT_NWB_UNKNOWN_OPTION in result.stderr, result
    assert "Press Enter to exit" not in result.stdout, result


def verify_application(executable: pathlib.Path, root: pathlib.Path) -> None:
    for arguments, expected_code in (([LIT_HELP], 0), ([LIT_NWB_UNKNOWN_OPTION], 109)):
        result = run(executable, arguments, root)
        assert result.returncode == expected_code, (executable, arguments, result)
        if arguments == [LIT_HELP]:
            assert LIT_HELP in result.stdout and not result.stderr, result
        else:
            assert LIT_NWB_UNKNOWN_OPTION in result.stderr, result
        output = result.stdout + result.stderr
        assert all(marker not in output for marker in LIT_STARTUP_MARKERS), (executable, arguments, result)


def main() -> None:
    args = parse_args()
    assert args.pipeline or args.utility or args.application, "provide at least one executable"
    with tempfile.TemporaryDirectory(prefix="nwb-terminal-") as directory:
        root = pathlib.Path(directory)
        for executable in args.pipeline:
            verify_pipeline(executable, root)
        for executable in args.utility:
            verify_utility(executable, root)
        for executable in args.application:
            verify_application(executable, root)


if __name__ == LIT_MAIN:
    main()

