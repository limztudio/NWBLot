"""Qualify opt-in UI skin texture closure through the actual asset pipeline tools."""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import struct
import sys
import types

LIT_UINT32_LITTLE_ENDIAN = "<I"

LIT_IMPL = "impl"
LIT_ASSETS = "assets"
LIT_UI = "ui"
LIT_TEXTURE_NWB = "texture.nwb"
LIT_TEXTURE_TEX = "texture.tex"
LIT_ASSETS_LIST = "assets.list"
LIT_C = "c"
LIT_TESTS = "tests"
LIT_PROJECT = "project"
LIT_ATLAS_NWB = "atlas.nwb"
LIT_LINEAR = "linear"
LIT_PROJECT_ASSETS_UI_UI_ATLAS_NWB = "project/assets/ui/../ui/atlas.nwb"
LIT_PROJECT_ASSETS_UI_UI = "project/assets/ui/../ui"
LIT_ORDER = "order"


UTF_8 = "utf-8"
INCLUDE_SKIN_DEPENDENCIES = "--include-skin-dependencies"
REPO_ROOT = "--repo-root"
ASSET_ROOT = "--asset-root"
INPUT = "--input"
INPUT_LIST = "--input-list"
OUTPUT = "--output"
OUTPUT_DIRECTORY = "--output-directory"
CACHE_DIRECTORY = "--cache-directory"
CONFIGURATION = "--configuration"
TEXTURE_IDENTITY = "engine/ui/texture"
TEXTURE_MAGIC = 0x54455831
TEXTURE_VERSION = 4
TEXTURE_HEADER = struct.Struct("<9IQ")
TEXTURE_MIP = struct.Struct("<6I2Q")
UASTC_BLOCK_BYTES = 16
SKIN_MAGIC = 0x55495331
SKIN_VERSION = 3
NAME_HASH_BYTES = 64
ATLAS_EXTENT = (256, 256)
ARTIFACT_MAGIC = 0x4142574E
ARTIFACT_VERSION = 1


def _write_metadata(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", "\r\n").encode(UTF_8))


def _write_skin(path: pathlib.Path, texture: str, extent: tuple[int, int] = ATLAS_EXTENT) -> None:
    roles = ("text.normal", "text.disabled", "text.tooltip", "edit.background", "edit.selection", "edit.inactive_selection", "edit.caret", "edit.preedit", "scrollbar.track", "scrollbar.thumb",
        "scrollbar.disabled", "popup.backdrop", "control.hover_tint", "control.pressed_tint", "control.disabled_tint", "progress.track_tint", "progress.fill_tint",)
    colors = [{"name": role, "rgba": [1.0, 1.0, 1.0, 1.0]} for role in roles]
    _write_metadata(path, ("ui_skin asset;\n" f'asset.texture = "{texture}";\n' f"asset.atlas_extent = [{extent[0]}, {extent[1]}];\n" "asset.reference_density = 1.0;\n" 'asset.regions = [{"name": "panel.normal", "rect": [0, 0, 24, 24]}];\n'
        f"asset.colors = {json.dumps(colors)};\n" 'asset.typography = {"default_font_size": 16.0};\n'))


def _copy_texture(repo: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    shipped = repo / LIT_IMPL / LIT_ASSETS / LIT_UI / "skins" / "default"
    path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(shipped / LIT_TEXTURE_NWB, path)
    sidecar = path.parent / LIT_TEXTURE_TEX
    shutil.copyfile(shipped / LIT_TEXTURE_TEX, sidecar)
    return sidecar


def _dependency_command(args: argparse.Namespace, fixture: pathlib.Path, roots: list[pathlib.Path], inputs: list[str], manifest: pathlib.Path, enabled: bool = True,) -> list[str]:
    command = [str(args.dependency_computer), INPUT, *inputs, OUTPUT, str(manifest)]
    if enabled:
        command.extend([INCLUDE_SKIN_DEPENDENCIES, REPO_ROOT, str(fixture)])
        for root in roots:
            command.extend([ASSET_ROOT, str(root)])
    return command


def _expect_manifest(manifest: pathlib.Path, inputs: list[str], appended: list[pathlib.Path]) -> None:
    actual = manifest.read_text(encoding=UTF_8).splitlines()
    if actual[:len(inputs)] != inputs or len(actual) != len(inputs) + len(appended):
        raise AssertionError(f"skin closure changed input spelling/order/duplicates or appended count: {actual}")
    for spelling, expected in zip(actual[len(inputs):], appended):
        actual_path = pathlib.Path(spelling)
        if not actual_path.is_absolute() or actual_path.resolve() != expected.resolve():
            raise AssertionError(f"skin closure appended a different provider: {spelling}; expected {expected}")


def _read_current_artifacts(directory: pathlib.Path, helpers: types.ModuleType) -> dict[bytes, bytes]:
    manifest = directory / LIT_ASSETS_LIST
    names = manifest.read_text(encoding=UTF_8).splitlines()
    if not names or any(not name for name in names):
        raise AssertionError("builder published an empty current artifact list")
    directory_root = directory.resolve()
    artifacts = {}
    for name in names:
        listed = pathlib.Path(name)
        if listed.is_absolute() or listed.name != name or listed.suffix != ".nwba":
            raise AssertionError(f"builder listed an invalid relative artifact filename: {name}")
        path = (directory / listed).resolve()
        if not path.is_relative_to(directory_root):
            raise AssertionError(f"builder listed an artifact outside its output directory: {name}")
        binary = path.read_bytes()
        if len(binary) < helpers.ARTIFACT_HEADER.size:
            raise AssertionError(f"truncated current asset artifact: {path}")
        magic, version, identity, payload_size, _ = helpers.ARTIFACT_HEADER.unpack_from(binary)
        if (magic != ARTIFACT_MAGIC or version != ARTIFACT_VERSION or payload_size != len(binary) - helpers.ARTIFACT_HEADER.size):
            raise AssertionError(f"invalid current runtime asset artifact: {path}")
        if identity in artifacts:
            raise AssertionError("builder listed duplicate current artifact identities")
        artifacts[identity] = binary[helpers.ARTIFACT_HEADER.size:]
    if list(directory.rglob(helpers.LIT_VOL)):
        raise AssertionError("asset builder unexpectedly produced a volume")
    return artifacts


def _cooked_texture_primary_range(texture: bytes) -> tuple[int, int]:
    if len(texture) < TEXTURE_HEADER.size:
        raise AssertionError("skin package contains a truncated cooked texture header")
    magic, version, dimension, width, height, depth, mip_count, _, payload_format, payload_size = TEXTURE_HEADER.unpack_from(texture)
    if magic != TEXTURE_MAGIC or version != TEXTURE_VERSION:
        raise AssertionError("skin package did not preserve the current cooked texture format")
    if (width, height) != ATLAS_EXTENT:
        raise AssertionError("packaged texture changed the copied sidecar dimensions")
    if dimension != 0 or depth != 1 or payload_format != 0:
        raise AssertionError("copied skin texture must use a two-dimensional LDR primary stream")
    payload_begin = TEXTURE_HEADER.size + mip_count * TEXTURE_MIP.size
    if mip_count == 0 or payload_begin > len(texture) or payload_size != len(texture) - payload_begin:
        raise AssertionError("skin package contains a truncated cooked texture mip table or payload")
    primary_size = 0
    for index in range(mip_count):
        _, _, _, _, _, reserved, offset, size = TEXTURE_MIP.unpack_from(texture, TEXTURE_HEADER.size + index * TEXTURE_MIP.size)
        if reserved != 0 or offset != primary_size or size == 0 or size % UASTC_BLOCK_BYTES != 0:
            raise AssertionError("copied skin texture has an invalid UASTC primary stream layout")
        primary_size += size
    if primary_size != payload_size:
        raise AssertionError("copied LDR skin texture has bytes outside its primary stream")
    return payload_begin, payload_begin + primary_size


def _package_identities(payloads: dict[bytes, bytes]) -> tuple[bytes, bytes]:
    if len(payloads) != 2:
        raise AssertionError("skin package must contain exactly the selected skin and its texture")
    by_magic = {}
    for identity, payload in payloads.items():
        if len(payload) < 8:
            raise AssertionError("skin package contains a truncated payload")
        magic = struct.unpack_from(LIT_UINT32_LITTLE_ENDIAN, payload)[0]
        if magic in by_magic:
            raise AssertionError("skin package contains duplicate payload types")
        by_magic[magic] = identity
    if set(by_magic) != {SKIN_MAGIC, TEXTURE_MAGIC}:
        raise AssertionError("skin package included an unrelated asset or omitted its texture")
    skin_identity = by_magic[SKIN_MAGIC]
    texture_identity = by_magic[TEXTURE_MAGIC]
    skin = payloads[skin_identity]
    texture = payloads[texture_identity]
    if len(skin) < 80:
        raise AssertionError("skin package contains a truncated skin or texture header")
    if struct.unpack_from(LIT_UINT32_LITTLE_ENDIAN, skin, 4)[0] != SKIN_VERSION:
        raise AssertionError("skin package did not write the current skin version")
    if skin[8:8 + NAME_HASH_BYTES] != texture_identity:
        raise AssertionError("packaged skin refers to a texture identity absent from the package")
    if struct.unpack_from("<II", skin, 72) != ATLAS_EXTENT:
        raise AssertionError("packaged skin changed the declared atlas extent")
    _cooked_texture_primary_range(texture)
    return skin_identity, texture_identity


def _run_package_tests(args: argparse.Namespace, root: pathlib.Path, fixture: pathlib.Path, roots: list[pathlib.Path], manifest: pathlib.Path, sidecar: pathlib.Path, invalid_skin: pathlib.Path, helpers: types.ModuleType,) -> None:
    artifacts = fixture / "built"
    packed = fixture / "volume"
    build_command = [str(args.asset_builder), REPO_ROOT, str(fixture), INPUT_LIST, str(manifest), OUTPUT_DIRECTORY, str(artifacts), CACHE_DIRECTORY, str(root / LIT_C), CONFIGURATION, LIT_TESTS,]
    for asset_root in roots:
        build_command.extend([ASSET_ROOT, str(asset_root)])
    gather_command = [str(args.asset_gatherer), INPUT_LIST, str(artifacts / LIT_ASSETS_LIST), OUTPUT_DIRECTORY, str(packed), CONFIGURATION, LIT_TESTS,]
    helpers.run_command(build_command, fixture)
    original = _read_current_artifacts(artifacts, helpers)
    skin_identity, texture_identity = _package_identities(original)
    payload_begin, primary_end = _cooked_texture_primary_range(original[texture_identity])
    if original[texture_identity][payload_begin:primary_end] != sidecar.read_bytes():
        raise AssertionError("cooker changed the paired raw texture sidecar bytes")
    helpers.run_command(gather_command, fixture)
    if helpers.read_volume(packed) != original:
        raise AssertionError("gathered skin volume differs from its exact two built payloads")

    blocks = bytearray(sidecar.read_bytes())
    if len(blocks) % UASTC_BLOCK_BYTES != 0:
        raise AssertionError("raw texture sidecar has an incomplete UASTC block")
    if len(blocks) < 2 * UASTC_BLOCK_BYTES:
        raise AssertionError("shipped texture sidecar has fewer than two complete UASTC blocks")
    first_block = blocks[:UASTC_BLOCK_BYTES]
    replacement = next((blocks[index:index + UASTC_BLOCK_BYTES] for index in range(UASTC_BLOCK_BYTES, len(blocks), UASTC_BLOCK_BYTES)
        if blocks[index:index + UASTC_BLOCK_BYTES] != first_block), None)
    if replacement is None:
        raise AssertionError("shipped texture has no distinct complete block for sidecar invalidation")
    blocks[:UASTC_BLOCK_BYTES] = replacement
    sidecar.write_bytes(blocks)
    helpers.run_command(build_command, fixture)
    refreshed = _read_current_artifacts(artifacts, helpers)
    if _package_identities(refreshed) != (skin_identity, texture_identity):
        raise AssertionError("sidecar rebuild changed the skin or texture identity")
    if refreshed[skin_identity] != original[skin_identity]:
        raise AssertionError("texture sidecar change unexpectedly changed the skin payload")
    if refreshed[texture_identity] == original[texture_identity]:
        raise AssertionError("texture sidecar change reused a stale cooked texture")
    payload_begin, primary_end = _cooked_texture_primary_range(refreshed[texture_identity])
    if refreshed[texture_identity][payload_begin:primary_end] != sidecar.read_bytes():
        raise AssertionError("sidecar rebuild did not preserve the changed raw texture bytes")
    helpers.run_command(gather_command, fixture)
    if helpers.read_volume(packed) != refreshed:
        raise AssertionError("texture sidecar change was not reflected in the republished volume")

    launcher_command = [sys.executable, str(args.pipeline_launcher), "--skip-build", INCLUDE_SKIN_DEPENDENCIES, REPO_ROOT, str(args.repo_root), INPUT, str(fixture / LIT_PROJECT / LIT_ASSETS / LIT_UI / LIT_ATLAS_NWB), OUTPUT_DIRECTORY, str(packed),
        CACHE_DIRECTORY, str(root / LIT_C), CONFIGURATION, LIT_TESTS, "--dependency-computer", str(args.dependency_computer), "--asset-builder", str(args.asset_builder), "--asset-gatherer", str(args.asset_gatherer),]
    for asset_root in roots:
        launcher_command.extend([ASSET_ROOT, str(asset_root)])
    helpers.run_command(launcher_command, fixture)
    if helpers.read_volume(packed) != refreshed:
        raise AssertionError("launcher did not forward skin dependency closure into the exact published volume")
    launcher_command[launcher_command.index(INPUT) + 1] = str(invalid_skin)
    helpers.run_command(launcher_command, fixture, failure=True)
    if helpers.read_volume(packed) != refreshed:
        raise AssertionError("failed skin closure changed the already published volume")


def run_skin_dependency_tests(args: argparse.Namespace, root: pathlib.Path, helpers: types.ModuleType) -> None:
    fixture = root / "s"
    project_root = fixture / LIT_PROJECT / LIT_ASSETS
    engine_root = fixture / LIT_IMPL / LIT_ASSETS
    roots = [project_root, engine_root]
    skin = project_root / LIT_UI / LIT_ATLAS_NWB
    shared_skin = project_root / LIT_UI / "shared.nwb"
    texture = engine_root / LIT_UI / LIT_TEXTURE_NWB
    unrelated = project_root / "samplers" / "unrelated.nwb"
    bunch = project_root / "bundles" / "selected.nwb"
    manifest = fixture / "dependencies.txt"
    sidecar = _copy_texture(args.repo_root, texture)
    _write_skin(skin, TEXTURE_IDENTITY)
    _write_skin(shared_skin, TEXTURE_IDENTITY)
    helpers.write_sampler(unrelated, LIT_LINEAR)
    _write_metadata(bunch, "sampler child; asset_bunch bundle = [child];\n")
    _write_metadata(project_root / "unrelated_bad.nwb", "unrelated malformed metadata {{{\n")
    _write_metadata(project_root / "shaders" / "unrelated_bad.bind", "unrelated malformed bind metadata {{{\n")

    original = [LIT_PROJECT_ASSETS_UI_UI_ATLAS_NWB, str(skin), LIT_PROJECT_ASSETS_UI_UI_ATLAS_NWB]
    helpers.run_command(_dependency_command(args, fixture, roots, original, manifest, enabled=False), fixture)
    _expect_manifest(manifest, original, [])
    helpers.run_command(_dependency_command(args, fixture, roots, original, manifest), fixture)
    _expect_manifest(manifest, original, [texture])
    good_manifest = manifest.read_bytes()

    cases = (([str(skin), str(shared_skin), str(skin)], roots, [texture]), ([str(skin), "impl/assets/ui/../ui/texture.nwb", str(skin)], roots, []), ([str(unrelated), str(unrelated)], roots, []), ([str(bunch)], roots, []),
        ([str(skin)], [project_root, engine_root, engine_root, engine_root / LIT_UI], [texture]), ([LIT_PROJECT_ASSETS_UI_UI], roots, [texture]), ([LIT_PROJECT_ASSETS_UI_UI, "impl/assets/ui"], roots, []),)
    scratch_manifest = fixture / "case.txt"
    for inputs, asset_roots, appended in cases:
        helpers.run_command(_dependency_command(args, fixture, asset_roots, inputs, scratch_manifest), fixture)
        _expect_manifest(scratch_manifest, inputs, appended)

    ordered_a = project_root / LIT_ORDER / "a.nwb"
    ordered_b = project_root / LIT_ORDER / "b.nwb"
    texture_a = project_root / "textures" / "a" / LIT_TEXTURE_NWB
    texture_b = engine_root / LIT_ORDER / "b" / LIT_TEXTURE_NWB
    _copy_texture(args.repo_root, texture_a)
    _copy_texture(args.repo_root, texture_b)
    _write_skin(ordered_a, "project/textures/a/texture")
    _write_skin(ordered_b, "engine/order/b/texture")
    sampler_root = fixture / "typed" / LIT_IMPL / LIT_ASSETS
    helpers.write_sampler(sampler_root / LIT_UI / LIT_TEXTURE_NWB, LIT_LINEAR)
    ordered_cases = (([str(ordered_b), str(ordered_a), str(ordered_b)], roots, [texture_b, texture_a]), ([str(project_root / LIT_ORDER)], roots, [texture_a, texture_b]), ([str(skin)], [project_root, sampler_root, engine_root], [texture]),)
    for inputs, asset_roots, appended in ordered_cases:
        helpers.run_command(_dependency_command(args, fixture, asset_roots, inputs, scratch_manifest), fixture)
        _expect_manifest(scratch_manifest, inputs, appended)

    missing_skin = project_root / "missing" / LIT_ATLAS_NWB
    _write_skin(missing_skin, "engine/ui/missing")
    wrong_skin = project_root / "wrong" / LIT_ATLAS_NWB
    _write_skin(wrong_skin, "engine/ui/wrong")
    helpers.write_sampler(engine_root / LIT_UI / "wrong.nwb", LIT_LINEAR)
    invalid_skin = project_root / "invalid" / LIT_ATLAS_NWB
    _write_skin(invalid_skin, TEXTURE_IDENTITY, (16, 16))
    multiple_skin = project_root / "multiple" / LIT_ATLAS_NWB
    _write_skin(multiple_skin, TEXTURE_IDENTITY)
    _write_metadata(multiple_skin, multiple_skin.read_text(encoding=UTF_8) + "sampler extra;\n")
    duplicate_root = fixture / "other" / LIT_IMPL / LIT_ASSETS
    _copy_texture(args.repo_root, duplicate_root / LIT_UI / LIT_TEXTURE_NWB)
    failures = (([str(missing_skin)], roots), ([str(wrong_skin)], roots), ([str(skin)], [*roots, duplicate_root]), ([str(invalid_skin)], roots), ([str(multiple_skin)], roots),)
    for inputs, asset_roots in failures:
        helpers.run_command(_dependency_command(args, fixture, asset_roots, inputs, manifest), fixture, failure=True,)
        if manifest.read_bytes() != good_manifest:
            raise AssertionError("failed skin dependency closure replaced the previously accepted dependency list")

    _run_package_tests(args, root, fixture, roots, manifest, sidecar, invalid_skin, helpers)

