#!/usr/bin/env python3

import argparse
import importlib.util
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
from unittest import mock


ASSET_TYPE_SENTINEL = "nwb_cli_test_unsupported"
CHILD_TIMEOUT_SECONDS = 30.0
ARTIFACT_HEADER = struct.Struct("<II64sQQ")
VOLUME_HEADER = struct.Struct("<8sQQQQQ")
VOLUME_INDEX_ENTRY = struct.Struct("<64sQQ")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dependency-computer", type=pathlib.Path, required=True)
    parser.add_argument("--asset-builder", type=pathlib.Path, required=True)
    parser.add_argument("--asset-gatherer", type=pathlib.Path, required=True)
    parser.add_argument("--cooker-script", type=pathlib.Path, required=True)
    parser.add_argument("--repo-root", type=pathlib.Path, required=True)
    return parser.parse_args()


def run_command(command: list[str], working_directory: pathlib.Path, failure: bool = False) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=working_directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=CHILD_TIMEOUT_SECONDS,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"pipeline command timed out: {command}") from error
    allowed_codes = (1, 255, 0xFFFFFFFF) if failure else (0,)
    if result.returncode not in allowed_codes:
        raise AssertionError(
            f"pipeline command returned {result.returncode}, expected {allowed_codes}: {command}; "
            f"output tail:\n{result.stdout[-4000:]}"
        )
    return result.stdout


def read_artifacts(directory: pathlib.Path) -> dict[bytes, bytes]:
    artifacts = {}
    for path in sorted(directory.rglob("*.nwba")):
        binary = path.read_bytes()
        if len(binary) < ARTIFACT_HEADER.size:
            raise AssertionError(f"truncated asset artifact: {path}")
        magic, version, name_hash, payload_size, _ = ARTIFACT_HEADER.unpack_from(binary)
        if magic != 0x4142574E or version != 1 or payload_size != len(binary) - ARTIFACT_HEADER.size:
            raise AssertionError(f"invalid runtime asset artifact: {path}")
        if name_hash in artifacts:
            raise AssertionError(f"duplicate artifact identity in {directory}")
        artifacts[name_hash] = binary[ARTIFACT_HEADER.size:]
    if not artifacts:
        raise AssertionError(f"builder produced no runtime artifacts in {directory}")
    if list(directory.rglob("*.vol")):
        raise AssertionError("asset builder unexpectedly produced a volume")
    return artifacts


def read_volume(directory: pathlib.Path) -> dict[bytes, bytes]:
    segments = sorted(directory.glob("*.vol"))
    if len(segments) != 1:
        raise AssertionError(f"expected one small graphics volume in {directory}, found {segments}")
    binary = segments[0].read_bytes()
    if len(binary) < VOLUME_HEADER.size:
        raise AssertionError("gatherer produced a truncated volume")
    magic, segment_size, metadata_size, file_count, index_size, next_free = VOLUME_HEADER.unpack_from(binary)
    if magic != b"NWBVOL1\0" or index_size != file_count * VOLUME_INDEX_ENTRY.size:
        raise AssertionError("gatherer produced an invalid volume header/index")
    if not VOLUME_HEADER.size + index_size <= metadata_size < segment_size or next_free != len(binary):
        raise AssertionError("gatherer produced invalid metadata or payload ranges")
    payloads = {}
    for index in range(file_count):
        name_hash, offset, size = VOLUME_INDEX_ENTRY.unpack_from(binary, VOLUME_HEADER.size + index * VOLUME_INDEX_ENTRY.size)
        if offset < metadata_size or offset + size > len(binary) or name_hash in payloads:
            raise AssertionError("gatherer produced invalid or duplicate volume entries")
        payloads[name_hash] = binary[offset:offset + size]
    return payloads


def sampler_filters(payloads: dict[bytes, bytes]) -> list[int]:
    return sorted(
        struct.unpack_from("<I", payload, 32)[0]
        for payload in payloads.values()
        if len(payload) == 64 and struct.unpack_from("<II", payload) == (0x53414D31, 1)
    )


def write_sampler(path: pathlib.Path, filtering: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    metadata = (
        "sampler asset;\n"
        f'asset.min_filter = "{filtering}";\n'
        f'asset.mag_filter = "{filtering}";\n'
        'asset.mip_filter = "linear";\n'
        'asset.address_u = "clamp";\n'
        'asset.address_v = "clamp";\n'
        'asset.address_w = "clamp";\n'
        'asset.reduction = "standard";\n'
        "asset.max_anisotropy = 1.0;\n"
        "asset.mip_bias = 0.0;\n"
        "asset.border_color = [0.0, 0.0, 0.0, 0.0];\n"
    )
    path.write_bytes(metadata.replace("\n", "\r\n").encode("utf-8"))


def run_discovery_failures(args: argparse.Namespace, root: pathlib.Path, asset_root: pathlib.Path, output: pathlib.Path) -> None:
    specification = importlib.util.spec_from_file_location("nwb_test_pipeline_cooker", args.cooker_script)
    if specification is None or specification.loader is None:
        raise AssertionError("could not load the cooker for discovery failure regression")
    cooker = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(cooker)
    options = argparse.Namespace(
        repo_root=args.repo_root, asset_root=[asset_root], output=output,
        cache_directory=root / "c", build_directory=None, configuration="tests",
        dependency_computer=args.dependency_computer, asset_builder=args.asset_builder,
        asset_gatherer=args.asset_gatherer, tool_directory=None, input=None, asset_type="graphics",
    )
    previous_volume = read_volume(output)
    with cooker.os.scandir(asset_root) as entries:
        readable_entry = next(iter(entries))
    inaccessible_entry = mock.Mock()
    inaccessible_entry.stat.side_effect = PermissionError("test entry cannot be inspected")
    partial_scan = mock.MagicMock()
    partial_scan.__enter__.return_value = (readable_entry, inaccessible_entry)
    scan_failures = (
        {"side_effect": PermissionError("test directory cannot be scanned")},
        {"return_value": partial_scan},
    )
    for scan_failure in scan_failures:
        with mock.patch.object(cooker.os, "scandir", **scan_failure), mock.patch.object(cooker, "run_stage") as stage:
            try:
                cooker.cook(options)
            except PermissionError:
                pass
            else:
                raise AssertionError("cooker accepted an incomplete asset discovery result")
            stage.assert_not_called()
        if read_volume(output) != previous_volume:
            raise AssertionError("a discovery failure changed an already published volume")


def run_pipeline_tests(args: argparse.Namespace, root: pathlib.Path) -> None:
    first_root = root / "first project" / "assets"
    second_root = root / "second project" / "assets"
    first_asset = first_root / "samplers" / "first.nwb"
    second_asset = second_root / "samplers" / "second.nwb"
    write_sampler(first_asset, "linear")
    write_sampler(second_asset, "nearest")

    manifest = root / "dependencies.txt"
    input_values = [str(second_asset), str(first_asset), str(second_asset)]
    run_command([str(args.dependency_computer), "--input", *input_values, "--output", str(manifest)], root)
    if manifest.read_text(encoding="utf-8").splitlines() != input_values:
        raise AssertionError("dependency computation must preserve every input, its order, and duplicate values")

    build_options = [
        "--repo-root", str(args.repo_root),
        "--asset-root", str(first_root), str(second_root),
        "--cache-directory", str(root / "c"),
        "--configuration", "tests",
    ]
    failure_output = run_command(
        [str(args.asset_builder), *build_options, "--input", str(first_asset),
         "--output-directory", str(root / "unsupported"), "--asset-type", ASSET_TYPE_SENTINEL],
        root,
        failure=True,
    )
    expected_message = f"unsupported --asset-type '{ASSET_TYPE_SENTINEL}'. Available types:"
    if expected_message not in failure_output or "[ERROR]:" not in failure_output:
        raise AssertionError(f"builder did not preserve handled unsupported-type diagnostics:\n{failure_output[-4000:]}")
    run_command(
        [str(args.asset_builder), *build_options, "--input", str(root / "missing.nwb"),
         "--output-directory", str(root / "missing-output")],
        root,
        failure=True,
    )

    combined_directory = root / "combined artifacts"
    run_command(
        [str(args.asset_builder), *build_options, "--input", str(first_asset), str(second_asset),
         "--output-directory", str(combined_directory)],
        root,
    )
    combined_payloads = read_artifacts(combined_directory)
    if sampler_filters(combined_payloads) != [0, 1]:
        raise AssertionError("multiple input assets did not produce both runtime sampler descriptions")

    individual_directories = []
    individual_payloads = {}
    for index, asset in enumerate((first_asset, second_asset)):
        directory = root / f"individual-{index}"
        list_path = root / f"input-{index}.txt"
        list_path.write_text(str(asset) + "\n", encoding="utf-8")
        run_command(
            [str(args.asset_builder), *build_options, "--input-list", str(list_path),
             "--output-directory", str(directory)],
            root,
        )
        payloads = read_artifacts(directory)
        if sampler_filters(payloads) != [1 - index]:
            raise AssertionError("explicit builder input selection included unselected assets or changed their data")
        for name_hash, payload in payloads.items():
            if name_hash in individual_payloads and individual_payloads[name_hash] != payload:
                raise AssertionError("independent builds disagreed on a shared artifact")
            individual_payloads[name_hash] = payload
        individual_directories.append(directory)
    if individual_payloads != combined_payloads:
        raise AssertionError("building multiple inputs differs from building the same assets independently")

    if sys.platform == "win32":
        case_directory = root / "case-input"
        run_command(
            [str(args.asset_builder), *build_options, "--input", str(first_root).upper(),
             "--output-directory", str(case_directory)],
            root,
        )
        if read_artifacts(case_directory) != read_artifacts(individual_directories[0]):
            raise AssertionError("Windows directory input selection changed when path spelling used uppercase letters")

    cooked_directory = root / "cooked"
    run_command(
        [sys.executable, str(args.cooker_script), *build_options,
         "--dependency-computer", str(args.dependency_computer),
         "--asset-builder", str(args.asset_builder), "--asset-gatherer", str(args.asset_gatherer),
         "--output-directory", str(cooked_directory)],
        root,
    )
    if read_volume(cooked_directory) != combined_payloads:
        raise AssertionError("cooker orchestration did not gather all built runtime payloads")

    first_asset.unlink()
    second_asset.unlink()
    gathered_directory = root / "gathered"
    run_command(
        [str(args.asset_gatherer), "--input", *(str(path) for path in individual_directories),
         "--output-directory", str(gathered_directory), "--configuration", "tests"],
        root,
    )
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("gathering independent build outputs changed or dropped runtime payloads")

    gather_list = root / "gather.txt"
    gather_list.write_text("".join(str(path) + "\n" for path in combined_directory.rglob("*.nwba")), encoding="utf-8")
    list_directory = root / "gathered-list"
    run_command(
        [str(args.asset_gatherer), "--input-list", str(gather_list), "--output-directory", str(list_directory)],
        root,
    )
    if read_volume(list_directory) != combined_payloads:
        raise AssertionError("gathering an explicit artifact list changed runtime payloads")

    write_sampler(first_asset, "nearest")
    run_command(
        [str(args.asset_builder), *build_options, "--input", str(first_asset),
         "--output-directory", str(combined_directory)],
        root,
    )
    refreshed_directory = root / "refreshed"
    run_command(
        [str(args.asset_gatherer), "--input", str(combined_directory),
         "--output-directory", str(refreshed_directory)],
        root,
    )
    refreshed_payloads = read_volume(refreshed_directory)
    if set(refreshed_payloads) != set(read_artifacts(individual_directories[0])) or sampler_filters(refreshed_payloads) != [0]:
        raise AssertionError("gathering a rebuilt directory retained stale assets or an old runtime payload")

    run_command(
        [str(args.asset_gatherer), "--input", str(individual_directories[0]), str(combined_directory),
         "--output-directory", str(gathered_directory)],
        root,
        failure=True,
    )
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("conflicting duplicate assets changed an already published volume")

    run_command(
        [str(args.asset_gatherer), "--input", str(root / "missing.nwba"),
         "--output-directory", str(gathered_directory)],
        root,
        failure=True,
    )
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("a missing gather input changed an already published volume")

    run_command(
        [sys.executable, str(args.cooker_script), *build_options,
         "--dependency-computer", str(args.dependency_computer),
         "--asset-builder", str(args.asset_builder), "--asset-gatherer", str(args.asset_gatherer),
         "--output-directory", str(cooked_directory), "--asset-type", ASSET_TYPE_SENTINEL],
        root,
        failure=True,
    )
    if read_volume(cooked_directory) != combined_payloads:
        raise AssertionError("a failed build stage did not preserve the previously cooked volume")

    corrupt_path = root / "corrupt.nwba"
    artifact_bytes = bytearray(next(combined_directory.rglob("*.nwba")).read_bytes())
    artifact_bytes[-1] ^= 0x01
    corrupt_path.write_bytes(artifact_bytes)
    run_command(
        [str(args.asset_gatherer), "--input", str(corrupt_path), "--output-directory", str(gathered_directory)],
        root,
        failure=True,
    )
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("a corrupt gather input changed an already published volume")

    moved_directory = root / "copied artifacts"
    shutil.copytree(combined_directory, moved_directory)
    for artifact in combined_directory.glob("*.nwba"):
        artifact.unlink()
    moved_volume_directory = root / "copied-volume"
    run_command(
        [str(args.asset_gatherer), "--input", str(moved_directory),
         "--output-directory", str(moved_volume_directory)],
        root,
    )
    if read_volume(moved_volume_directory) != refreshed_payloads:
        raise AssertionError("copied build outputs still depended on their original artifact directory")

    run_discovery_failures(args, root, first_asset.parent, cooked_directory)


def main() -> int:
    args = parse_args()
    try:
        for name in ("dependency_computer", "asset_builder", "asset_gatherer", "cooker_script"):
            path = getattr(args, name).resolve()
            if not path.is_file():
                raise AssertionError(f"pipeline tool does not exist: {path}")
            setattr(args, name, path)
        args.repo_root = args.repo_root.resolve()
        # The object-cache directory includes a 128-character type hash; keep Windows paths below MAX_PATH.
        temporary_parent = args.repo_root / "__build_obj" / "c"
        temporary_parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="p", dir=temporary_parent) as temporary_directory:
            run_pipeline_tests(args, pathlib.Path(temporary_directory))
    except (AssertionError, OSError, struct.error) as error:
        print(f"pipeline CLI integration failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
