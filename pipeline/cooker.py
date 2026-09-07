#!/usr/bin/env python3
"""Build NWB assets and publish a runtime volume through the three pipeline tools."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    parser.add_argument("--asset-root", type=Path, action="extend", nargs="+", required=True)
    parser.add_argument("--input", type=Path, action="extend", nargs="+",
                        help="Explicit .nwb files or directories; defaults to all asset roots")
    parser.add_argument("-o", "--output", "--output-directory", type=Path, required=True)
    parser.add_argument("--cache-directory", type=Path)
    parser.add_argument("--build-directory", type=Path,
                        help="Persistent directory for standalone .nwba artifacts")
    parser.add_argument("--configuration", default="")
    parser.add_argument("--asset-type", default="graphics")
    parser.add_argument("--tool-directory", type=Path,
                        help="Directory containing the three built pipeline executables")
    parser.add_argument("--dependency-computer", "--dependeny-computer", type=Path)
    parser.add_argument("--asset-builder", type=Path)
    parser.add_argument("--asset-gatherer", type=Path)
    return parser.parse_args()


def resolve_path(root: Path, path: Path) -> Path:
    return (path if path.is_absolute() else root / path).resolve()


def find_tool(name: str, explicit: Path | None, directory: Path | None) -> Path:
    if explicit is not None:
        candidate = explicit.resolve()
        if candidate.is_file():
            return candidate
        raise ValueError(f"Pipeline executable does not exist: {candidate}")

    filenames = (f"{name}.exe", name) if sys.platform == "win32" else (name,)
    directories = [directory] if directory is not None else [Path(__file__).resolve().parent]
    for folder in directories:
        for filename in filenames:
            candidate = folder.resolve() / filename
            if candidate.is_file():
                return candidate
    if directory is None:
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
    raise ValueError(f"Cannot find {name}; provide --tool-directory or its executable path")


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
    print(f"cooker: {executable.stem}", flush=True)
    subprocess.run([str(executable), *arguments], cwd=repo_root, check=True)


def cook(options: argparse.Namespace) -> None:
    repo_root = options.repo_root.resolve()
    roots = [resolve_path(repo_root, root) for root in options.asset_root]
    output = resolve_path(repo_root, options.output)
    cache = resolve_path(repo_root, options.cache_directory or Path("__build_obj/asset_cache"))
    output_key = hashlib.sha256(str(output).encode("utf-8")).hexdigest()[:16]
    configuration_key = hashlib.sha256(options.configuration.encode("utf-8")).hexdigest()[:16]
    built = resolve_path(repo_root, options.build_directory) if options.build_directory else cache / "pipeline" / configuration_key / output_key
    dependency = find_tool("dependeny_computer", options.dependency_computer, options.tool_directory)
    builder = find_tool("asset_builder", options.asset_builder, options.tool_directory)
    gatherer = find_tool("asset_gatherer", options.asset_gatherer, options.tool_directory)
    inputs = discover_inputs(roots, options.input, repo_root)

    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pipeline-", dir=cache) as temporary:
        workspace = Path(temporary)
        initial_list = workspace / "inputs.list"
        dependency_list = workspace / "dependencies.list"
        write_inputs(initial_list, inputs)
        run_stage(dependency, ["--input-list", str(initial_list), "--output", str(dependency_list)], repo_root)
        builder_arguments = [
            "--input-list", str(dependency_list), "--repo-root", str(repo_root),
            "--output-directory", str(built), "--cache-directory", str(cache),
            "--asset-type", options.asset_type,
        ]
        for root in roots:
            builder_arguments.extend(["--asset-root", str(root)])
        if options.configuration:
            builder_arguments.extend(["--configuration", options.configuration])
        run_stage(builder, builder_arguments, repo_root)
        gatherer_arguments = ["--input-list", str(built / "assets.list"), "--output-directory", str(output)]
        if options.configuration:
            gatherer_arguments.extend(["--configuration", options.configuration])
        run_stage(gatherer, gatherer_arguments, repo_root)


def main() -> int:
    options = parse_arguments()
    try:
        cook(options)
    except (OSError, ValueError) as error:
        print(f"cooker: {error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"cooker: {Path(error.cmd[0]).stem} failed with exit code {error.returncode}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

