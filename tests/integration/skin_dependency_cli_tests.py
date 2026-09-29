"""Qualify opt-in UI skin texture closure through the actual asset pipeline tools."""
from __future__ import annotations

import argparse
import pathlib
import shutil
import struct
import sys
import types


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
SKIN_MAGIC = 0x55495331
NAME_HASH_BYTES = 64
ATLAS_EXTENT = (256, 256)
ARTIFACT_MAGIC = 0x4142574E
ARTIFACT_VERSION = 1


def _write_metadata(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", "\r\n").encode(UTF_8))


def _write_skin(path: pathlib.Path, texture: str, extent: tuple[int, int] = ATLAS_EXTENT) -> None:
    _write_metadata(path, (
        "ui_skin asset;\n"
        "asset.schema_version = 1;\n"
        f'asset.texture = "{texture}";\n'
        f"asset.atlas_extent = [{extent[0]}, {extent[1]}];\n"
        "asset.reference_density = 1.0;\n"
        'asset.regions = [{"name": "panel.normal", "rect": [0, 0, 24, 24]}];\n'
    ))


def _copy_texture(repo: pathlib.Path, path: pathlib.Path) -> pathlib.Path:
    shipped = repo / "impl" / "assets" / "ui" / "skins" / "default"
    path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(shipped / "texture.nwb", path)
    sidecar = path.parent / "texture.tex"
    shutil.copyfile(shipped / "texture.tex", sidecar)
    return sidecar


def _dependency_command(
    args: argparse.Namespace,
    fixture: pathlib.Path,
    roots: list[pathlib.Path],
    inputs: list[str],
    manifest: pathlib.Path,
    enabled: bool = True,
) -> list[str]:
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
    manifest = directory / "assets.list"
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
        if (
            magic != ARTIFACT_MAGIC
            or version != ARTIFACT_VERSION
            or payload_size != len(binary) - helpers.ARTIFACT_HEADER.size
        ):
            raise AssertionError(f"invalid current runtime asset artifact: {path}")
        if identity in artifacts:
            raise AssertionError("builder listed duplicate current artifact identities")
        artifacts[identity] = binary[helpers.ARTIFACT_HEADER.size:]
    if list(directory.rglob(helpers.LIT_VOL)):
        raise AssertionError("asset builder unexpectedly produced a volume")
    return artifacts


def _package_identities(payloads: dict[bytes, bytes]) -> tuple[bytes, bytes]:
    if len(payloads) != 2:
        raise AssertionError("skin package must contain exactly the selected skin and its texture")
    by_magic = {}
    for identity, payload in payloads.items():
        if len(payload) < 8:
            raise AssertionError("skin package contains a truncated payload")
        magic = struct.unpack_from("<I", payload)[0]
        if magic in by_magic:
            raise AssertionError("skin package contains duplicate payload types")
        by_magic[magic] = identity
    if set(by_magic) != {SKIN_MAGIC, TEXTURE_MAGIC}:
        raise AssertionError("skin package included an unrelated asset or omitted its texture")
    skin_identity = by_magic[SKIN_MAGIC]
    texture_identity = by_magic[TEXTURE_MAGIC]
    skin = payloads[skin_identity]
    texture = payloads[texture_identity]
    if len(skin) < 80 or len(texture) < 24:
        raise AssertionError("skin package contains a truncated skin or texture header")
    if skin[8:8 + NAME_HASH_BYTES] != texture_identity:
        raise AssertionError("packaged skin refers to a texture identity absent from the package")
    if struct.unpack_from("<II", skin, 72) != ATLAS_EXTENT:
        raise AssertionError("packaged skin changed the declared atlas extent")
    if struct.unpack_from("<II", texture, 16) != ATLAS_EXTENT:
        raise AssertionError("packaged texture changed the copied sidecar dimensions")
    return skin_identity, texture_identity


def _run_package_tests(
    args: argparse.Namespace,
    root: pathlib.Path,
    fixture: pathlib.Path,
    roots: list[pathlib.Path],
    manifest: pathlib.Path,
    sidecar: pathlib.Path,
    invalid_skin: pathlib.Path,
    helpers: types.ModuleType,
) -> None:
    artifacts = fixture / "built"
    packed = fixture / "volume"
    build_command = [
        str(args.asset_builder), REPO_ROOT, str(fixture), INPUT_LIST, str(manifest),
        OUTPUT_DIRECTORY, str(artifacts), CACHE_DIRECTORY, str(root / "c"), CONFIGURATION, "tests",
    ]
    for asset_root in roots:
        build_command.extend([ASSET_ROOT, str(asset_root)])
    gather_command = [
        str(args.asset_gatherer), INPUT_LIST, str(artifacts / "assets.list"), OUTPUT_DIRECTORY, str(packed),
        CONFIGURATION, "tests",
    ]
    helpers.run_command(build_command, fixture)
    original = _read_current_artifacts(artifacts, helpers)
    skin_identity, texture_identity = _package_identities(original)
    helpers.run_command(gather_command, fixture)
    if helpers.read_volume(packed) != original:
        raise AssertionError("gathered skin volume differs from its exact two built payloads")

    blocks = bytearray(sidecar.read_bytes())
    if len(blocks) < 32:
        raise AssertionError("shipped texture sidecar has fewer than two complete UASTC blocks")
    replacement = next((blocks[index:index + 16] for index in range(16, len(blocks), 16)
                        if blocks[index:index + 16] != blocks[:16]), None)
    if replacement is None:
        raise AssertionError("shipped texture has no distinct complete block for sidecar invalidation")
    blocks[:16] = replacement
    sidecar.write_bytes(blocks)
    helpers.run_command(build_command, fixture)
    refreshed = _read_current_artifacts(artifacts, helpers)
    if _package_identities(refreshed) != (skin_identity, texture_identity):
        raise AssertionError("sidecar rebuild changed the skin or texture identity")
    if refreshed[skin_identity] != original[skin_identity]:
        raise AssertionError("texture sidecar change unexpectedly changed the skin payload")
    if refreshed[texture_identity] == original[texture_identity]:
        raise AssertionError("texture sidecar change reused a stale cooked texture")
    helpers.run_command(gather_command, fixture)
    if helpers.read_volume(packed) != refreshed:
        raise AssertionError("texture sidecar change was not reflected in the republished volume")

    launcher_command = [
        sys.executable, str(args.pipeline_launcher), "--skip-build", INCLUDE_SKIN_DEPENDENCIES,
        REPO_ROOT, str(args.repo_root), INPUT, str(fixture / "project" / "assets" / "ui" / "atlas.nwb"),
        OUTPUT_DIRECTORY, str(packed),
        CACHE_DIRECTORY, str(root / "c"), CONFIGURATION, "tests",
        "--dependency-computer", str(args.dependency_computer), "--asset-builder", str(args.asset_builder),
        "--asset-gatherer", str(args.asset_gatherer),
    ]
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
    project_root = fixture / "project" / "assets"
    engine_root = fixture / "impl" / "assets"
    roots = [project_root, engine_root]
    skin = project_root / "ui" / "atlas.nwb"
    shared_skin = project_root / "ui" / "shared.nwb"
    texture = engine_root / "ui" / "texture.nwb"
    unrelated = project_root / "samplers" / "unrelated.nwb"
    manifest = fixture / "dependencies.txt"
    sidecar = _copy_texture(args.repo_root, texture)
    _write_skin(skin, TEXTURE_IDENTITY)
    _write_skin(shared_skin, TEXTURE_IDENTITY)
    helpers.write_sampler(unrelated, "linear")
    _write_metadata(project_root / "unrelated_bad.nwb", "unrelated malformed metadata {{{\n")
    _write_metadata(project_root / "shaders" / "unrelated_bad.bind", "unrelated malformed bind metadata {{{\n")

    original = ["project/assets/ui/../ui/atlas.nwb", str(skin), "project/assets/ui/../ui/atlas.nwb"]
    helpers.run_command(_dependency_command(args, fixture, roots, original, manifest, enabled=False), fixture)
    _expect_manifest(manifest, original, [])
    helpers.run_command(_dependency_command(args, fixture, roots, original, manifest), fixture)
    _expect_manifest(manifest, original, [texture])
    good_manifest = manifest.read_bytes()

    cases = (
        ([str(skin), str(shared_skin), str(skin)], roots, [texture]),
        ([str(skin), "impl/assets/ui/../ui/texture.nwb", str(skin)], roots, []),
        ([str(unrelated), str(unrelated)], roots, []),
        ([str(skin)], [project_root, engine_root, engine_root, engine_root / "ui"], [texture]),
        (["project/assets/ui/../ui"], roots, [texture]),
        (["project/assets/ui/../ui", "impl/assets/ui"], roots, []),
    )
    scratch_manifest = fixture / "case.txt"
    for inputs, asset_roots, appended in cases:
        helpers.run_command(_dependency_command(args, fixture, asset_roots, inputs, scratch_manifest), fixture)
        _expect_manifest(scratch_manifest, inputs, appended)

    ordered_a = project_root / "order" / "a.nwb"
    ordered_b = project_root / "order" / "b.nwb"
    texture_a = project_root / "textures" / "a" / "texture.nwb"
    texture_b = engine_root / "order" / "b" / "texture.nwb"
    _copy_texture(args.repo_root, texture_a)
    _copy_texture(args.repo_root, texture_b)
    _write_skin(ordered_a, "project/textures/a/texture")
    _write_skin(ordered_b, "engine/order/b/texture")
    sampler_root = fixture / "typed" / "impl" / "assets"
    helpers.write_sampler(sampler_root / "ui" / "texture.nwb", "linear")
    ordered_cases = (
        ([str(ordered_b), str(ordered_a), str(ordered_b)], roots, [texture_b, texture_a]),
        ([str(project_root / "order")], roots, [texture_a, texture_b]),
        ([str(skin)], [project_root, sampler_root, engine_root], [texture]),
    )
    for inputs, asset_roots, appended in ordered_cases:
        helpers.run_command(_dependency_command(args, fixture, asset_roots, inputs, scratch_manifest), fixture)
        _expect_manifest(scratch_manifest, inputs, appended)

    missing_skin = project_root / "missing" / "atlas.nwb"
    _write_skin(missing_skin, "engine/ui/missing")
    wrong_skin = project_root / "wrong" / "atlas.nwb"
    _write_skin(wrong_skin, "engine/ui/wrong")
    helpers.write_sampler(engine_root / "ui" / "wrong.nwb", "linear")
    invalid_skin = project_root / "invalid" / "atlas.nwb"
    _write_skin(invalid_skin, TEXTURE_IDENTITY, (16, 16))
    multiple_skin = project_root / "multiple" / "atlas.nwb"
    _write_skin(multiple_skin, TEXTURE_IDENTITY)
    _write_metadata(multiple_skin, multiple_skin.read_text(encoding=UTF_8) + "sampler extra;\n")
    duplicate_root = fixture / "other" / "impl" / "assets"
    _copy_texture(args.repo_root, duplicate_root / "ui" / "texture.nwb")
    failures = (
        ([str(missing_skin)], roots),
        ([str(wrong_skin)], roots),
        ([str(skin)], [*roots, duplicate_root]),
        ([str(invalid_skin)], roots),
        ([str(multiple_skin)], roots),
    )
    for inputs, asset_roots in failures:
        helpers.run_command(
            _dependency_command(args, fixture, asset_roots, inputs, manifest), fixture, failure=True,
        )
        if manifest.read_bytes() != good_manifest:
            raise AssertionError("failed skin dependency closure replaced the previously accepted dependency list")

    _run_package_tests(args, root, fixture, roots, manifest, sidecar, invalid_skin, helpers)

