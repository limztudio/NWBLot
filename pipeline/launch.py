#!/usr/bin/env python3
"""Build NWB assets and publish a runtime volume through the three pipeline tools."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Sequence


REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

import launcher as ROOT_LAUNCHER  # noqa: E402


TARGET = "nwb_pipeline"
EXIT_SUCCESS = 0
EXIT_FAILURE = 1
CACHE_KEY_HEX_DIGITS = 16
TEXT_ENCODING = "utf-8"
MAIN_ENTRY = "__main__"
INPUT_LIST_LINE_END = "\n"
ASSET_EXTENSION = ".nwb"
INPUT_LIST_NAME = "inputs.list"
ASSET_CACHE_DIRECTORY = "__asset_cache"
ACTION_EXTEND = "extend"
OPTION_OUTPUT_SHORT = "-o"
OPTION_OUTPUT = "--output"
OPTION_OUTPUT_DIRECTORY = "--output-directory"
OPTION_ASSET_ROOT = "--asset-root"
OPTION_CONFIGURATION = "--configuration"
OPTION_ASSET_TYPE = "--asset-type"
OPTION_INPUT_LIST = "--input-list"
OPTION_REPO_ROOT = "--repo-root"


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    ROOT_LAUNCHER.add_build_options(parser)
    parser.add_argument(OPTION_ASSET_ROOT, type=Path, action=ACTION_EXTEND, nargs="+", required=True)
    parser.add_argument("--input", type=Path, action=ACTION_EXTEND, nargs="+", help="Explicit .nwb files or directories; defaults to the project.nwb root manifest")
    parser.add_argument(OPTION_OUTPUT_SHORT, OPTION_OUTPUT, OPTION_OUTPUT_DIRECTORY, type=Path, required=True)
    parser.add_argument("--build-directory", type=Path, help="Persistent directory for standalone .nwba artifacts")
    parser.add_argument(OPTION_CONFIGURATION, help="Asset configuration label; defaults to the selected --config")
    parser.add_argument(OPTION_ASSET_TYPE, default="graphics")
    parser.add_argument("--tool-directory", type=Path, help="Directory containing the three built pipeline executables")
    parser.add_argument("--dependency-computer", type=Path)
    parser.add_argument("--asset-builder", type=Path)
    parser.add_argument("--asset-gatherer", type=Path)
    return parser


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    return make_parser().parse_args(argv)


def resolve_path(root: Path, path: Path) -> Path:
    return (path if path.is_absolute() else root / path).resolve()


def resolve_tools(options: argparse.Namespace, settings: ROOT_LAUNCHER.LaunchSettings) -> tuple[Path, Path, Path]:
    tools = []
    for target, explicit in (("nwb_dependency_computer", options.dependency_computer), ("nwb_asset_builder", options.asset_builder), ("nwb_asset_gatherer", options.asset_gatherer),):
        if explicit is None and options.tool_directory is not None:
            name = ROOT_LAUNCHER.target_default_executable_base_name(target)
            candidate = resolve_path(settings.root, options.tool_directory) / ROOT_LAUNCHER.executable_name(name, settings.platform_name)
        else:
            candidate = ROOT_LAUNCHER.resolve_executable_path(settings, target, explicit, None, options.dry_run)
        if not options.dry_run and not candidate.is_file():
            raise ValueError(f"Pipeline executable does not exist: {candidate}")
        tools.append(candidate)
    return tuple(tools)


def resolve_inputs(roots: list[Path], inputs: list[Path] | None, repo_root: Path) -> list[Path]:
    for root in roots:
        if not root.is_dir():
            raise ValueError(f"Asset root is not a directory: {root}")
    if inputs is None:
        return []

    resolved: list[Path] = []
    for value in inputs:
        candidate = resolve_path(repo_root, value)
        if not candidate.is_dir() and not (candidate.is_file() and candidate.suffix.lower() == ASSET_EXTENSION):
            raise ValueError(f"Input is not a .nwb file or directory: {candidate}")
        resolved.append(candidate)
    return resolved


def write_inputs(path: Path, values: list[Path]) -> None:
    lines = []
    for value in values:
        spelling = str(value)
        if INPUT_LIST_LINE_END in spelling or "\r" in spelling:
            raise ValueError("Asset paths must not contain newlines")
        lines.append(spelling + INPUT_LIST_LINE_END)
    path.write_text("".join(lines), encoding=TEXT_ENCODING, newline=INPUT_LIST_LINE_END)


def run_stage(executable: Path, arguments: list[str], repo_root: Path) -> None:
    print(f"pipeline: {executable.stem}", flush=True)
    subprocess.run([str(executable), *arguments], cwd=repo_root, check=True)


def pipeline_commands(options: argparse.Namespace, repo_root: Path, roots: list[Path], output: Path, built: Path, configuration: str, tools: tuple[Path, Path, Path], workspace: Path,) -> list[tuple[Path, list[str]]]:
    dependency, builder, gatherer = tools
    dependency_list = workspace / "dependencies.list"
    dependency_arguments = [OPTION_REPO_ROOT, str(repo_root), OPTION_OUTPUT, str(dependency_list)]
    if options.input is not None:
        initial_list = workspace / INPUT_LIST_NAME
        dependency_arguments.extend([OPTION_INPUT_LIST, str(initial_list)])
    for root in roots:
        dependency_arguments.extend([OPTION_ASSET_ROOT, str(root)])
    builder_arguments = [OPTION_INPUT_LIST, str(dependency_list), OPTION_REPO_ROOT, str(repo_root), OPTION_OUTPUT_DIRECTORY, str(built), OPTION_ASSET_TYPE, options.asset_type,]
    for root in roots:
        builder_arguments.extend([OPTION_ASSET_ROOT, str(root)])
    gatherer_arguments = [OPTION_INPUT_LIST, str(built / "assets.list"), OPTION_OUTPUT_DIRECTORY, str(output)]
    if configuration:
        builder_arguments.extend([OPTION_CONFIGURATION, configuration])
        gatherer_arguments.extend([OPTION_CONFIGURATION, configuration])
    return [(dependency, dependency_arguments), (builder, builder_arguments), (gatherer, gatherer_arguments),]


def cook(options: argparse.Namespace) -> None:
    settings = ROOT_LAUNCHER.resolve_launch_settings(options, ROOT_LAUNCHER.DEFAULT_DOMAIN)
    repo_root = settings.root
    roots = [resolve_path(repo_root, root) for root in options.asset_root]
    inputs = resolve_inputs(roots, options.input, repo_root)
    configuration = settings.config if options.configuration is None else options.configuration
    output = resolve_path(repo_root, options.output)
    cache = repo_root / ASSET_CACHE_DIRECTORY
    output_key = hashlib.sha256(str(output).encode(TEXT_ENCODING)).hexdigest()[:CACHE_KEY_HEX_DIGITS]
    configuration_key = hashlib.sha256(configuration.encode(TEXT_ENCODING)).hexdigest()[:CACHE_KEY_HEX_DIGITS]
    built = resolve_path(repo_root, options.build_directory) if options.build_directory else cache / "pipeline" / configuration_key / output_key

    env = ROOT_LAUNCHER.build_environment(options)
    if not options.skip_build:
        ROOT_LAUNCHER.maybe_configure(options, settings, {"NWB_BUILD_PIPELINE": "ON"}, env)
        settings = ROOT_LAUNCHER.refresh_launch_settings(settings, options.domain)
        ROOT_LAUNCHER.build_target(options, settings, TARGET, env)
    tools = resolve_tools(options, settings)
    if options.dry_run:
        workspace = cache / "pipeline-dry-run"
        for executable, arguments in pipeline_commands(options, repo_root, roots, output, built, configuration, tools, workspace):
            ROOT_LAUNCHER.run_checked([executable, *arguments], repo_root, env, dry_run=True)
        return

    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pipeline-", dir=cache) as temporary:
        workspace = Path(temporary)
        if options.input is not None:
            write_inputs(workspace / INPUT_LIST_NAME, inputs)
        for executable, arguments in pipeline_commands(options, repo_root, roots, output, built, configuration, tools, workspace):
            run_stage(executable, arguments, repo_root)


def main(argv: Sequence[str]) -> int:
    options = parse_arguments(argv)
    try:
        cook(options)
    except (OSError, ValueError) as error:
        print(f"pipeline: {error}", file=sys.stderr)
        return EXIT_FAILURE
    except subprocess.CalledProcessError as error:
        print(f"pipeline: {Path(error.cmd[0]).stem} failed with exit code {error.returncode}", file=sys.stderr)
        return EXIT_FAILURE
    return EXIT_SUCCESS


if __name__ == MAIN_ENTRY:
    raise SystemExit(main(sys.argv[1:]))
