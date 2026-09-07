#!/usr/bin/env python3
"""Build NWB assets and publish a runtime volume through the three pipeline tools."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
from typing import Sequence


REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


TARGET = "nwb_pipeline"


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument("--asset-root", type=Path, action="extend", nargs="+", required=True)
    parser.add_argument("--input", type=Path, action="extend", nargs="+",
                        help="Explicit .nwb files or directories; defaults to all asset roots")
    parser.add_argument("-o", "--output", "--output-directory", type=Path, required=True)
    parser.add_argument("--cache-directory", type=Path)
    parser.add_argument("--build-directory", type=Path,
                        help="Persistent directory for standalone .nwba artifacts")
    parser.add_argument("--configuration", help="Asset configuration label; defaults to the selected --config")
    parser.add_argument("--asset-type", default="graphics")
    parser.add_argument("--tool-directory", type=Path,
                        help="Directory containing the three built pipeline executables")
    parser.add_argument("--dependency-computer", "--dependeny-computer", type=Path)
    parser.add_argument("--asset-builder", type=Path)
    parser.add_argument("--asset-gatherer", type=Path)
    return parser


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    return make_parser().parse_args(argv)


def resolve_path(root: Path, path: Path) -> Path:
    return (path if path.is_absolute() else root / path).resolve()


def resolve_tools(options: argparse.Namespace, settings: ROOT_LAUNCHER.LaunchSettings) -> tuple[Path, Path, Path]:
    tools = []
    for target, explicit in (
        ("nwb_dependeny_computer", options.dependency_computer),
        ("nwb_asset_builder", options.asset_builder),
        ("nwb_asset_gatherer", options.asset_gatherer),
    ):
        if explicit is None and options.tool_directory is not None:
            name = ROOT_LAUNCHER.target_default_executable_base_name(target)
            candidate = resolve_path(settings.root, options.tool_directory) / ROOT_LAUNCHER.executable_name(name, settings.platform_name)
        else:
            candidate = ROOT_LAUNCHER.resolve_executable_path(settings, target, explicit, None, options.dry_run)
        if not options.dry_run and not candidate.is_file():
            raise ValueError(f"Pipeline executable does not exist: {candidate}")
        tools.append(candidate)
    return tuple(tools)


def discover_directory(directory: Path) -> list[Path]:
    files = []
    directories = [directory]
    while directories:
        with os.scandir(directories.pop()) as entries:
            for entry in entries:
                mode = entry.stat(follow_symlinks=False).st_mode
                if stat.S_ISDIR(mode):
                    directories.append(Path(entry.path))
                elif Path(entry.name).suffix.lower() == ".nwb":
                    if stat.S_ISLNK(mode):
                        mode = entry.stat().st_mode
                    if stat.S_ISREG(mode):
                        files.append(Path(entry.path).resolve())
    return sorted(files)


def discover_inputs(roots: list[Path], inputs: list[Path] | None, repo_root: Path) -> list[Path]:
    for root in roots:
        if not root.is_dir():
            raise ValueError(f"Asset root is not a directory: {root}")

    selected = [resolve_path(repo_root, value) for value in inputs] if inputs is not None else roots
    files: dict[Path, None] = {}
    for value in selected:
        if value.is_dir():
            for path in discover_directory(value):
                files[path] = None
        elif value.is_file() and value.suffix.lower() == ".nwb":
            files[value] = None
        else:
            raise ValueError(f"Input is not a .nwb file or directory: {value}")
    return list(files)


def write_inputs(path: Path, values: list[Path]) -> None:
    text = ""
    for value in values:
        spelling = str(value)
        if "\n" in spelling or "\r" in spelling:
            raise ValueError("Asset paths must not contain newlines")
        text += spelling + "\n"
    path.write_text(text, encoding="utf-8", newline="\n")


def run_stage(executable: Path, arguments: list[str], repo_root: Path) -> None:
    print(f"pipeline: {executable.stem}", flush=True)
    subprocess.run([str(executable), *arguments], cwd=repo_root, check=True)


def pipeline_commands(
    options: argparse.Namespace,
    repo_root: Path,
    roots: list[Path],
    output: Path,
    cache: Path,
    built: Path,
    configuration: str,
    tools: tuple[Path, Path, Path],
    workspace: Path,
) -> list[tuple[Path, list[str]]]:
    dependency, builder, gatherer = tools
    initial_list = workspace / "inputs.list"
    dependency_list = workspace / "dependencies.list"
    builder_arguments = [
        "--input-list", str(dependency_list), "--repo-root", str(repo_root),
        "--output-directory", str(built), "--cache-directory", str(cache),
        "--asset-type", options.asset_type,
    ]
    for root in roots:
        builder_arguments.extend(["--asset-root", str(root)])
    gatherer_arguments = ["--input-list", str(built / "assets.list"), "--output-directory", str(output)]
    if configuration:
        builder_arguments.extend(["--configuration", configuration])
        gatherer_arguments.extend(["--configuration", configuration])
    return [
        (dependency, ["--input-list", str(initial_list), "--output", str(dependency_list)]),
        (builder, builder_arguments),
        (gatherer, gatherer_arguments),
    ]


def cook(options: argparse.Namespace) -> None:
    settings = ROOT_LAUNCHER.resolve_launch_settings(options, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    repo_root = settings.root
    roots = [resolve_path(repo_root, root) for root in options.asset_root]
    inputs = discover_inputs(roots, options.input, repo_root)
    configuration = settings.config if options.configuration is None else options.configuration
    output = resolve_path(repo_root, options.output)
    cache = resolve_path(repo_root, options.cache_directory or Path("__build_obj/asset_cache"))
    output_key = hashlib.sha256(str(output).encode("utf-8")).hexdigest()[:16]
    configuration_key = hashlib.sha256(configuration.encode("utf-8")).hexdigest()[:16]
    built = resolve_path(repo_root, options.build_directory) if options.build_directory else cache / "pipeline" / configuration_key / output_key

    env = ROOT_LAUNCHER.build_environment(options)
    if not options.skip_build:
        ROOT_LAUNCHER.maybe_configure(options, settings, {"NWB_BUILD_PIPELINE": "ON"}, env)
        settings = ROOT_LAUNCHER.refresh_launch_settings(settings, options.domain)
        ROOT_LAUNCHER.build_target(options, settings, TARGET, env)
    tools = resolve_tools(options, settings)
    if options.dry_run:
        workspace = cache / "pipeline-dry-run"
        for executable, arguments in pipeline_commands(options, repo_root, roots, output, cache, built, configuration, tools, workspace):
            ROOT_LAUNCHER.run_checked([executable, *arguments], repo_root, env, dry_run=True)
        return

    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pipeline-", dir=cache) as temporary:
        workspace = Path(temporary)
        write_inputs(workspace / "inputs.list", inputs)
        for executable, arguments in pipeline_commands(options, repo_root, roots, output, cache, built, configuration, tools, workspace):
            run_stage(executable, arguments, repo_root)


def main(argv: Sequence[str]) -> int:
    options = parse_arguments(argv)
    try:
        cook(options)
    except (OSError, ValueError) as error:
        print(f"pipeline: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"pipeline: {Path(error.cmd[0]).stem} failed with exit code {error.returncode}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

