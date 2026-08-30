#!/usr/bin/env python3

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile


ASSET_TYPE_SENTINEL = "nwb_cli_test_unsupported"
CHILD_TIMEOUT_SECONDS = 10.0
CHILD_REAP_TIMEOUT_SECONDS = 5.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=pathlib.Path, required=True)
    parser.add_argument("--repo-root", type=pathlib.Path, required=True)
    return parser.parse_args()


def expected_failure_code() -> int:
    return 0xFFFFFFFF if os.name == "nt" else 255


def output_tail(output: str, limit: int = 4000) -> str:
    return output[-limit:]


def run_unsupported_asset_type(executable: pathlib.Path, repo_root: pathlib.Path) -> None:
    executable = executable.resolve()
    repo_root = repo_root.resolve()
    asset_root = repo_root / "impl" / "assets"

    if not executable.is_file():
        raise AssertionError(f"resource cooker executable does not exist: {executable}")
    if not asset_root.is_dir():
        raise AssertionError(f"resource cooker asset root does not exist: {asset_root}")

    with tempfile.TemporaryDirectory(prefix="nwb_resource_cooker_cli_") as temporary_directory:
        temporary_root = pathlib.Path(temporary_directory)
        output_directory = temporary_root / "output"
        cache_directory = temporary_root / "cache"
        output_directory.mkdir()
        cache_directory.mkdir()

        command = [
            str(executable),
            "--repo-root",
            str(repo_root),
            "--asset-root",
            str(asset_root),
            "--output-directory",
            str(output_directory),
            "--cache-directory",
            str(cache_directory),
            "--configuration",
            "tests",
            "--asset-type",
            ASSET_TYPE_SENTINEL,
        ]

        process = subprocess.Popen(
            command,
            cwd=temporary_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        try:
            output, _ = process.communicate(timeout=CHILD_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            try:
                output, _ = process.communicate(timeout=CHILD_REAP_TIMEOUT_SECONDS)
            except subprocess.TimeoutExpired as error:
                raise AssertionError("resource cooker could not be reaped after its timeout") from error
            raise AssertionError(
                f"resource cooker did not exit within {CHILD_TIMEOUT_SECONDS:.0f}s; output tail:\n{output_tail(output)}"
            )

    normal_failure_code = expected_failure_code()
    if process.returncode != normal_failure_code:
        raise AssertionError(
            f"resource cooker exited with {process.returncode}, expected handled failure {normal_failure_code}; "
            f"output tail:\n{output_tail(output)}"
        )

    required_messages = (
        f"Unsupported --asset-type '{ASSET_TYPE_SENTINEL}'. Available types:",
        "Resource cooker: asset cook failed",
    )
    for message in required_messages:
        if message not in output:
            raise AssertionError(f"resource cooker output is missing '{message}'; output tail:\n{output_tail(output)}")

    if output.count("[ERROR]:") < len(required_messages):
        raise AssertionError(f"resource cooker did not preserve error severity; output tail:\n{output_tail(output)}")


def main() -> int:
    args = parse_args()
    try:
        run_unsupported_asset_type(args.executable, args.repo_root)
    except (AssertionError, OSError) as error:
        print(f"resource cooker CLI integration failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
