"""Read and validate built UI skin and texture package evidence."""
from __future__ import annotations

import pathlib
import struct
import types


LIT_UINT32_LITTLE_ENDIAN = "<I"
LIT_ASSETS_LIST = "assets.list"
UTF_8 = "utf-8"
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


class SkinPackageEvidence:
    @staticmethod
    def read_current_artifacts(directory: pathlib.Path, helpers: types.ModuleType) -> dict[bytes, bytes]:
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

    @staticmethod
    def cooked_texture_primary_range(texture: bytes) -> tuple[int, int]:
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

    @staticmethod
    def package_identities(payloads: dict[bytes, bytes]) -> tuple[bytes, bytes]:
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
        SkinPackageEvidence.cooked_texture_primary_range(texture)
        return skin_identity, texture_identity

