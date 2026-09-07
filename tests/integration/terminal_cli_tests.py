#!/usr/bin/env python3

import argparse
import pathlib
import subprocess
import tempfile


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pipeline", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--utility", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--application", type=pathlib.Path, action="append", default=[])
    return parser.parse_args()


def run(executable: pathlib.Path, arguments: list[str], root: pathlib.Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(executable.resolve()), *arguments], cwd=root, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=30.0, check=False,
    )


def verify_pipeline(executable: pathlib.Path, root: pathlib.Path) -> None:
    help_result = run(executable, ["--help"], root)
    assert help_result.returncode == 0, (executable, help_result)
    assert "NWB asset pipeline" in help_result.stdout and "--output" in help_result.stdout, help_result
    assert "Usage:" not in help_result.stderr, help_result
    for arguments in ([], ["--nwb-unknown-option"], ["--output", str(root / "output")]):
        result = run(executable, arguments, root)
        assert result.returncode == 1, (executable, arguments, result)
        if "--output" not in arguments:
            assert "NWB asset pipeline" in result.stderr and "--output" in result.stderr, result
            assert "failed to parse command line" in result.stdout + result.stderr, result
        else:
            assert "provide --input or --input-list" in result.stdout + result.stderr, result
        assert not (root / "output").exists(), (executable, arguments, "created output after failure")


def verify_utility(executable: pathlib.Path, root: pathlib.Path) -> None:
    help_result = run(executable, ["--help"], root)
    assert help_result.returncode == 0 and "--help" in help_result.stdout, (executable, help_result)
    result = run(executable, ["--nwb-unknown-option"], root)
    # CLI11's ExtrasError is 109; preserve its native terminal code instead of normalizing it to generic failure.
    assert result.returncode == 109, (executable, result)
    assert "--nwb-unknown-option" in result.stderr, result
    assert "Press Enter to exit" not in result.stdout, result


def verify_application(executable: pathlib.Path, root: pathlib.Path) -> None:
    for arguments in (["--help"], ["--nwb-unknown-option"]):
        result = run(executable, arguments, root)
        assert result.returncode in (-1, 255, 0xFFFFFFFF), (executable, arguments, result)
        if arguments == ["--help"]:
            assert "--help" in result.stdout, result
        else:
            assert "--nwb-unknown-option" in result.stderr, result


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


if __name__ == "__main__":
    main()

