#!/usr/bin/env python3

import argparse
import hashlib
import importlib.util
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
from unittest import mock

from skin_dependency_cli_tests import run_skin_dependency_tests

LIT_DEPENDENCY_COMPUTER = "--dependency-computer"
LIT_ASSET_BUILDER = "--asset-builder"
LIT_ASSET_GATHERER = "--asset-gatherer"
LIT_REPO_ROOT = "--repo-root"
LIT_UTF_8 = "utf-8"
LIT_NWBA = "*.nwba"
LIT_VOL = "*.vol"
LIT_LINEAR = "linear"
LIT_CLAMP = "clamp"
LIT_N = "\n"
LIT_SKIP_BUILD = "--skip-build"
LIT_ASSET_ROOT = "--asset-root"
LIT_OUTPUT_DIRECTORY = "--output-directory"
LIT_CONFIGURATION = "--configuration"
LIT_TESTS = "tests"
LIT_ASSETS = "assets"
LIT_SAMPLERS = "samplers"
LIT_NEAREST = "nearest"
LIT_INPUT = "--input"
LIT_ASSET_TYPE = "--asset-type"
LIT_INPUT_LIST = "--input-list"
LIT_MAIN = "__main__"
LIT_EMPTY_ASSET_TYPE = ""


ASSET_CACHE_DIRECTORY = "__asset_cache"
LIT_ASSETS_LIST = "assets.list"
ASSET_TYPE_SENTINEL = "nwb_cli_test_unsupported"
CHILD_TIMEOUT_SECONDS = 30.0
ARTIFACT_MAGIC = 0x4142574E
ARTIFACT_VERSION = 2
ARTIFACT_HEADER = struct.Struct("<IHHII32sQQ")
ARTIFACT_ENTRY_PREFIX = struct.Struct("<I64s64sI")
ARTIFACT_ENTRY_SUFFIX = struct.Struct("<QQ")
FNV64_OFFSET_BASIS = 14695981039346656037
FNV64_PRIME = 1099511628211
VOLUME_HEADER = struct.Struct("<8sQQQQQ")
VOLUME_INDEX_ENTRY = struct.Struct("<64sQQ")


def compute_fnv64(data: bytes) -> int:
    value = FNV64_OFFSET_BASIS
    for byte in data:
        value ^= byte
        value = (value * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def encode_artifact_bytes(
    flags: int,
    param_sha256: bytes,
    source_path: bytes,
    entries: list[tuple[int, bytes, bytes, bytes, bytes]],
) -> bytes:
    payload_section = bytearray()
    payload_section.extend(struct.pack("<I", len(source_path)))
    payload_section.extend(source_path)
    for sub_index, asset_type_hash, virtual_path_hash, virtual_path_text, payload in entries:
        payload_section.extend(ARTIFACT_ENTRY_PREFIX.pack(sub_index, asset_type_hash, virtual_path_hash, len(virtual_path_text)))
        payload_section.extend(virtual_path_text)
        payload_section.extend(ARTIFACT_ENTRY_SUFFIX.pack(len(payload), compute_fnv64(payload)))
        payload_section.extend(payload)
    header = ARTIFACT_HEADER.pack(
        ARTIFACT_MAGIC,
        ARTIFACT_VERSION,
        ARTIFACT_HEADER.size,
        flags,
        len(entries),
        param_sha256,
        len(payload_section),
        compute_fnv64(payload_section),
    )
    return header + bytes(payload_section)


def decode_artifact_bytes(binary: bytes, source: pathlib.Path | str = "<memory>") -> list[tuple[bytes, bytes]]:
    if len(binary) < ARTIFACT_HEADER.size:
        raise AssertionError(f"truncated asset artifact: {source}")
    magic, version, header_size, _, entry_count, _, section_size, section_hash = ARTIFACT_HEADER.unpack_from(binary)
    if magic != ARTIFACT_MAGIC or version != ARTIFACT_VERSION or header_size != ARTIFACT_HEADER.size or entry_count == 0:
        raise AssertionError(f"invalid runtime asset artifact: {source}")
    cursor = ARTIFACT_HEADER.size
    if section_size != len(binary) - cursor or compute_fnv64(binary[cursor:]) != section_hash:
        raise AssertionError(f"invalid runtime asset artifact section checksum: {source}")
    if cursor + 4 > len(binary):
        raise AssertionError(f"truncated source path in artifact: {source}")
    source_len = struct.unpack_from("<I", binary, cursor)[0]
    cursor += 4 + source_len
    if cursor > len(binary):
        raise AssertionError(f"truncated source path in artifact: {source}")
    entries: list[tuple[bytes, bytes]] = []
    for _ in range(entry_count):
        if cursor + ARTIFACT_ENTRY_PREFIX.size > len(binary):
            raise AssertionError(f"truncated entry prefix in artifact: {source}")
        _, _, virtual_path_hash, text_len = ARTIFACT_ENTRY_PREFIX.unpack_from(binary, cursor)
        cursor += ARTIFACT_ENTRY_PREFIX.size + text_len
        if cursor + ARTIFACT_ENTRY_SUFFIX.size > len(binary):
            raise AssertionError(f"truncated entry header in artifact: {source}")
        payload_size, payload_hash = ARTIFACT_ENTRY_SUFFIX.unpack_from(binary, cursor)
        cursor += ARTIFACT_ENTRY_SUFFIX.size
        if cursor + payload_size > len(binary):
            raise AssertionError(f"truncated entry payload in artifact: {source}")
        payload = binary[cursor:cursor + payload_size]
        cursor += payload_size
        if compute_fnv64(payload) != payload_hash:
            raise AssertionError(f"corrupt entry payload checksum in artifact: {source}")
        entries.append((virtual_path_hash, payload))
    if cursor != len(binary):
        raise AssertionError(f"trailing bytes in artifact: {source}")
    return entries


def expected_cache_path(repo_root: pathlib.Path, source_path: pathlib.Path) -> pathlib.Path:
    canonical = str(source_path.resolve()).replace("\\", "/").lower()
    digest = hashlib.sha1(canonical.encode(LIT_UTF_8)).hexdigest()
    return repo_root / ASSET_CACHE_DIRECTORY / f"{digest}.nwba"


def read_manifest_artifact_paths(directory: pathlib.Path) -> list[pathlib.Path]:
    manifest = directory / LIT_ASSETS_LIST
    if not manifest.is_file():
        raise AssertionError(f"missing assets.list in {directory}")
    lines = manifest.read_text(encoding=LIT_UTF_8).splitlines()
    if not lines or any(not line for line in lines):
        raise AssertionError(f"builder published an empty artifact list in {directory}")
    resolved: list[pathlib.Path] = []
    for line in lines:
        listed = pathlib.Path(line)
        path = listed.resolve() if listed.is_absolute() else (directory / listed).resolve()
        if path.suffix != ".nwba":
            raise AssertionError(f"builder listed a non-.nwba artifact: {line}")
        resolved.append(path)
    return resolved


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(LIT_DEPENDENCY_COMPUTER, type=pathlib.Path, required=True)
    parser.add_argument(LIT_ASSET_BUILDER, type=pathlib.Path, required=True)
    parser.add_argument(LIT_ASSET_GATHERER, type=pathlib.Path, required=True)
    parser.add_argument("--pipeline-launcher", type=pathlib.Path, required=True)
    parser.add_argument(LIT_REPO_ROOT, type=pathlib.Path, required=True)
    return parser.parse_args()


def run_command(command: list[str], working_directory: pathlib.Path, failure: bool = False) -> str:
    try:
        result = subprocess.run(command, cwd=working_directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding=LIT_UTF_8, errors="replace", timeout=CHILD_TIMEOUT_SECONDS, check=False,)
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"pipeline command timed out: {command}") from error
    allowed_codes = (1, 255, 0xFFFFFFFF) if failure else (0,)
    if result.returncode not in allowed_codes:
        raise AssertionError(f"pipeline command returned {result.returncode}, expected {allowed_codes}: {command}; " f"output tail:\n{result.stdout[-4000:]}")
    return result.stdout


def read_artifacts(directory: pathlib.Path) -> dict[bytes, bytes]:
    artifacts = {}
    manifest = directory / LIT_ASSETS_LIST
    paths = read_manifest_artifact_paths(directory) if manifest.is_file() else sorted(directory.rglob(LIT_NWBA))
    for path in paths:
        for name_hash, payload in decode_artifact_bytes(path.read_bytes(), path):
            if name_hash in artifacts:
                raise AssertionError(f"duplicate artifact identity in {directory}")
            artifacts[name_hash] = payload
    if not artifacts:
        raise AssertionError(f"builder produced no runtime artifacts in {directory}")
    if list(directory.rglob(LIT_VOL)):
        raise AssertionError("asset builder unexpectedly produced a volume")
    return artifacts


def read_volume(directory: pathlib.Path) -> dict[bytes, bytes]:
    segments = sorted(directory.glob(LIT_VOL))
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
    return sorted(struct.unpack_from("<I", payload, 32)[0] for payload in payloads.values() if len(payload) == 64 and struct.unpack_from("<II", payload) == (0x53414D31, 1))


def write_sampler(path: pathlib.Path, filtering: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    metadata = ("sampler asset;\n" f'asset.min_filter = "{filtering}";\n' f'asset.mag_filter = "{filtering}";\n' f'asset.mip_filter = "{LIT_LINEAR}";\n' f'asset.address_u = "{LIT_CLAMP}";\n' f'asset.address_v = "{LIT_CLAMP}";\n'
        f'asset.address_w = "{LIT_CLAMP}";\n' 'asset.reduction = "standard";\n' "asset.max_anisotropy = 1.0;\n" "asset.mip_bias = 0.0;\n")
    path.write_bytes(metadata.replace(LIT_N, "\r\n").encode(LIT_UTF_8))


def run_discovery_failures(args: argparse.Namespace, root: pathlib.Path, asset_root: pathlib.Path, output: pathlib.Path) -> None:
    specification = importlib.util.spec_from_file_location("nwb_test_pipeline_launcher", args.pipeline_launcher)
    if specification is None or specification.loader is None:
        raise AssertionError("could not load the pipeline launcher for discovery failure regression")
    pipeline_launcher = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(pipeline_launcher)
    previous_volume = read_volume(output)
    missing_root = root / "missing_asset_root"
    missing_root_options = pipeline_launcher.parse_arguments([LIT_SKIP_BUILD, LIT_REPO_ROOT, str(root), LIT_ASSET_ROOT, str(missing_root), LIT_OUTPUT_DIRECTORY, str(output), LIT_CONFIGURATION, LIT_TESTS,
        LIT_DEPENDENCY_COMPUTER, str(args.dependency_computer), LIT_ASSET_BUILDER, str(args.asset_builder), LIT_ASSET_GATHERER, str(args.asset_gatherer),])
    invalid_input_options = pipeline_launcher.parse_arguments([LIT_SKIP_BUILD, LIT_REPO_ROOT, str(root), LIT_ASSET_ROOT, str(asset_root), LIT_INPUT, str(root / "missing_input.nwb"), LIT_OUTPUT_DIRECTORY, str(output), LIT_CONFIGURATION, LIT_TESTS,
        LIT_DEPENDENCY_COMPUTER, str(args.dependency_computer), LIT_ASSET_BUILDER, str(args.asset_builder), LIT_ASSET_GATHERER, str(args.asset_gatherer),])
    for bad_options in (missing_root_options, invalid_input_options):
        with mock.patch.object(pipeline_launcher, "run_stage") as stage:
            try:
                pipeline_launcher.cook(bad_options)
            except ValueError:
                pass
            else:
                raise AssertionError("pipeline launcher accepted a missing asset root or invalid input")
            stage.assert_not_called()
        if read_volume(output) != previous_volume:
            raise AssertionError("a preflight validation failure changed an already published volume")


def run_pipeline_tests(args: argparse.Namespace, root: pathlib.Path) -> None:
    first_root = root / "first project" / LIT_ASSETS
    second_root = root / "second project" / LIT_ASSETS
    first_asset = first_root / LIT_SAMPLERS / "first.nwb"
    second_asset = second_root / LIT_SAMPLERS / "second.nwb"
    write_sampler(first_asset, LIT_LINEAR)
    write_sampler(second_asset, LIT_NEAREST)

    manifest = root / "dependencies.txt"
    input_values = [str(second_asset), str(first_asset), str(second_asset)]
    run_command([str(args.dependency_computer), LIT_INPUT, *input_values, "--output", str(manifest)], root)
    if manifest.read_text(encoding=LIT_UTF_8).splitlines() != input_values:
        raise AssertionError("dependency computation must preserve every input, its order, and duplicate values")

    build_options = [LIT_REPO_ROOT, str(root), LIT_ASSET_ROOT, str(first_root), str(second_root), LIT_CONFIGURATION, LIT_TESTS,]
    failure_output = run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(first_asset), LIT_OUTPUT_DIRECTORY, str(root / "unsupported"), LIT_ASSET_TYPE, ASSET_TYPE_SENTINEL], root, failure=True,)
    expected_message = f"unsupported --asset-type '{ASSET_TYPE_SENTINEL}'. Available types:"
    if expected_message not in failure_output or "[ERROR]:" not in failure_output:
        raise AssertionError(f"builder did not preserve handled unsupported-type diagnostics:\n{failure_output[-4000:]}")
    run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(root / "missing.nwb"), LIT_OUTPUT_DIRECTORY, str(root / "missing-output")], root, failure=True,)

    combined_directory = root / "combined artifacts"
    run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(first_asset), str(second_asset), LIT_OUTPUT_DIRECTORY, str(combined_directory)], root,)
    first_cache_path = expected_cache_path(root, first_asset)
    second_cache_path = expected_cache_path(root, second_asset)
    if not first_cache_path.is_file() or not second_cache_path.is_file():
        raise AssertionError("standalone cooked assets were not placed directly under __asset_cache/<sha1>.nwba")
    if list(combined_directory.rglob(LIT_NWBA)):
        raise AssertionError("asset builder duplicated standalone .nwba files outside __asset_cache")
    if set(read_manifest_artifact_paths(combined_directory)) != {first_cache_path.resolve(), second_cache_path.resolve()}:
        raise AssertionError("assets.list did not reference the shared __asset_cache .nwba files")
    combined_payloads = read_artifacts(combined_directory)
    if sampler_filters(combined_payloads) != [0, 1]:
        raise AssertionError("multiple input assets did not produce both runtime sampler descriptions")

    exemplar = first_cache_path.read_bytes()
    name_hash, _ = decode_artifact_bytes(exemplar, first_cache_path)[0]
    empty_artifact = encode_artifact_bytes(0, b"\0" * 32, b"", [(0, b"\0" * 64, name_hash, b"", b"")])
    first_empty = root / "empty-first.nwba"
    second_empty = root / "empty-second.nwba"
    first_empty.write_bytes(empty_artifact)
    second_empty.write_bytes(empty_artifact)
    empty_output = root / "empty-gathered"
    run_command([str(args.asset_gatherer), LIT_INPUT, str(first_empty), str(second_empty), LIT_OUTPUT_DIRECTORY, str(empty_output)], root)
    if read_volume(empty_output) != {name_hash: b""}:
        raise AssertionError("identical empty payloads did not gather into one empty asset")

    previous_build_files = {path.relative_to(combined_directory): path.read_bytes() for path in combined_directory.rglob("*") if path.is_file()}
    failure_output = run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(first_asset), LIT_OUTPUT_DIRECTORY, str(combined_directory), LIT_ASSET_TYPE, LIT_EMPTY_ASSET_TYPE], root, failure=True,)
    if "unsupported --asset-type ''" not in failure_output:
        raise AssertionError(f"builder accepted or misreported an explicitly empty domain:\n{failure_output[-4000:]}")
    rejected_build_files = {path.relative_to(combined_directory): path.read_bytes() for path in combined_directory.rglob("*") if path.is_file()}
    if rejected_build_files != previous_build_files:
        raise AssertionError("an empty build domain changed existing artifacts or the manifest")

    individual_directories = []
    individual_payloads = {}
    for index, asset in enumerate((first_asset, second_asset)):
        directory = root / f"individual-{index}"
        list_path = root / f"input-{index}.txt"
        list_path.write_text(str(asset) + LIT_N, encoding=LIT_UTF_8)
        cache_hit_output = run_command([str(args.asset_builder), *build_options, LIT_INPUT_LIST, str(list_path), LIT_OUTPUT_DIRECTORY, str(directory)], root,)
        if "[INFO]:" not in cache_hit_output or "is already cached" not in cache_hit_output:
            raise AssertionError(f"builder did not notify cached asset via info(...):\n{cache_hit_output[-4000:]}")
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
        run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(first_root).upper(), LIT_OUTPUT_DIRECTORY, str(case_directory)], root,)
        if read_artifacts(case_directory) != read_artifacts(individual_directories[0]):
            raise AssertionError("Windows directory input selection changed when path spelling used uppercase letters")

    cooked_directory = root / "cooked"
    run_command([sys.executable, str(args.pipeline_launcher), LIT_SKIP_BUILD, *build_options, LIT_INPUT, str(first_asset), str(second_asset), LIT_DEPENDENCY_COMPUTER, str(args.dependency_computer), LIT_ASSET_BUILDER, str(args.asset_builder), LIT_ASSET_GATHERER, str(args.asset_gatherer),
         LIT_OUTPUT_DIRECTORY, str(cooked_directory)], root,)
    if (root / ASSET_CACHE_DIRECTORY / "pipeline").exists():
        raise AssertionError("pipeline launcher created per-project intermediate cache subdirectories")
    if read_volume(cooked_directory) != combined_payloads:
        raise AssertionError("pipeline orchestration did not gather all built runtime payloads")

    first_asset.unlink()
    second_asset.unlink()
    gathered_directory = root / "gathered"
    run_command([str(args.asset_gatherer), LIT_INPUT, *(str(path) for path in individual_directories), LIT_OUTPUT_DIRECTORY, str(gathered_directory), LIT_CONFIGURATION, LIT_TESTS], root,)
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("gathering independent build outputs changed or dropped runtime payloads")

    write_sampler(first_asset, LIT_NEAREST)
    run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(first_asset), LIT_OUTPUT_DIRECTORY, str(combined_directory)], root,)
    refreshed_directory = root / "refreshed"
    run_command([str(args.asset_gatherer), LIT_INPUT, str(combined_directory), LIT_OUTPUT_DIRECTORY, str(refreshed_directory)], root,)
    refreshed_payloads = read_volume(refreshed_directory)
    if set(refreshed_payloads) != set(read_artifacts(individual_directories[0])) or sampler_filters(refreshed_payloads) != [0]:
        raise AssertionError("gathering a rebuilt directory retained stale assets or an old runtime payload")

    conflict_asset = second_root / LIT_SAMPLERS / "first.nwb"
    write_sampler(conflict_asset, LIT_LINEAR)
    conflict_directory = root / "conflict artifacts"
    run_command([str(args.asset_builder), *build_options, LIT_INPUT, str(conflict_asset), LIT_OUTPUT_DIRECTORY, str(conflict_directory)], root,)
    run_command([str(args.asset_gatherer), LIT_INPUT, str(conflict_directory), str(combined_directory), LIT_OUTPUT_DIRECTORY, str(gathered_directory)], root, failure=True,)
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("conflicting duplicate assets changed an already published volume")

    run_command([str(args.asset_gatherer), LIT_INPUT, str(root / "missing.nwba"), LIT_OUTPUT_DIRECTORY, str(gathered_directory)], root, failure=True,)
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("a missing gather input changed an already published volume")

    run_command([sys.executable, str(args.pipeline_launcher), LIT_SKIP_BUILD, *build_options, LIT_INPUT, str(first_asset), LIT_DEPENDENCY_COMPUTER, str(args.dependency_computer), LIT_ASSET_BUILDER, str(args.asset_builder), LIT_ASSET_GATHERER, str(args.asset_gatherer),
         LIT_OUTPUT_DIRECTORY, str(cooked_directory), LIT_ASSET_TYPE, ASSET_TYPE_SENTINEL], root, failure=True,)
    if read_volume(cooked_directory) != combined_payloads:
        raise AssertionError("a failed build stage did not preserve the previously cooked volume")

    failure_output = run_command([sys.executable, str(args.pipeline_launcher), LIT_SKIP_BUILD, *build_options, LIT_INPUT, str(first_asset), LIT_DEPENDENCY_COMPUTER, str(args.dependency_computer), LIT_ASSET_BUILDER, str(args.asset_builder), LIT_ASSET_GATHERER, str(args.asset_gatherer),
         LIT_OUTPUT_DIRECTORY, str(cooked_directory), LIT_ASSET_TYPE, LIT_EMPTY_ASSET_TYPE], root, failure=True,)
    if "unsupported --asset-type ''" not in failure_output:
        raise AssertionError(f"pipeline launcher accepted or replaced an explicitly empty domain:\n{failure_output[-4000:]}")
    if read_volume(cooked_directory) != combined_payloads:
        raise AssertionError("an empty launcher build domain changed the previously cooked volume")

    corrupt_path = root / "corrupt.nwba"
    artifact_bytes = bytearray(read_manifest_artifact_paths(combined_directory)[0].read_bytes())
    artifact_bytes[-1] ^= 0x01
    corrupt_path.write_bytes(artifact_bytes)
    run_command([str(args.asset_gatherer), LIT_INPUT, str(corrupt_path), LIT_OUTPUT_DIRECTORY, str(gathered_directory)], root, failure=True,)
    if read_volume(gathered_directory) != combined_payloads:
        raise AssertionError("a corrupt gather input changed an already published volume")

    moved_directory = root / "copied artifacts"
    shutil.copytree(combined_directory, moved_directory)
    for artifact in combined_directory.glob(LIT_NWBA):
        artifact.unlink()
    moved_volume_directory = root / "copied-volume"
    run_command([str(args.asset_gatherer), LIT_INPUT, str(moved_directory), LIT_OUTPUT_DIRECTORY, str(moved_volume_directory)], root,)
    if read_volume(moved_volume_directory) != refreshed_payloads:
        raise AssertionError("copied build outputs still depended on their original artifact directory")

    run_discovery_failures(args, root, first_asset.parent, cooked_directory)


def main() -> int:
    args = parse_args()
    try:
        for name in ("dependency_computer", "asset_builder", "asset_gatherer", "pipeline_launcher"):
            path = getattr(args, name).resolve()
            if not path.is_file():
                raise AssertionError(f"pipeline tool does not exist: {path}")
            setattr(args, name, path)
        args.repo_root = args.repo_root.resolve()
        with tempfile.TemporaryDirectory(prefix="nwb_pipeline_cli_") as temporary_directory:
            run_pipeline_tests(args, pathlib.Path(temporary_directory))
            run_skin_dependency_tests(args, pathlib.Path(temporary_directory), sys.modules[__name__])
    except (AssertionError, OSError, struct.error) as error:
        print(f"pipeline CLI integration failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == LIT_MAIN:
    sys.exit(main())
