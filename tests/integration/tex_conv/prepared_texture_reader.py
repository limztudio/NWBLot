"""Read and validate prepared texture metadata and payload layout."""

from __future__ import annotations

import json
import pathlib
import re

LIT_METADATA_SUFFIX = ".nwb"
LIT_FORMAT = "format"
LIT_DIMENSION = "dimension"
LIT_RESOLUTION = "resolution"
LIT_ALPHA_MODE = "alpha_mode"
LIT_ALPHA_CONSTANT_UNORM8 = "alpha_constant_unorm8"
LIT_CUBE = "cube"
LIT_VOLUME = "volume"
LIT_ALPHA_CONSTANT = "alpha_constant"
LIT_MIPS = "mips"


class PreparedTextureReader:
    @staticmethod
    def read_raw_pair(path: pathlib.Path) -> dict:
        metadata_path = path.with_suffix(LIT_METADATA_SUFFIX)
        metadata = metadata_path.read_text(encoding="utf-8")
        assignments = re.findall(r"asset\.(\w+)\s*=\s*([^;]+);", metadata)
        fields = dict(assignments)
        required = {LIT_FORMAT, LIT_DIMENSION, LIT_RESOLUTION, LIT_ALPHA_MODE}
        if len(fields) != len(assignments) or set(fields) not in (required, required | {LIT_ALPHA_CONSTANT_UNORM8}):
            raise AssertionError(f"texture metadata contains missing, duplicate, or unsupported fields: {metadata_path}\n{metadata}")
        try:
            values = {name: json.loads(value) for name, value in fields.items()}
        except json.JSONDecodeError as error:
            raise AssertionError(f"texture metadata contains malformed descriptor values: {metadata_path}") from error
        formats = {"uastc_ldr_4x4": 0, "uastc_hdr_4x4": 1}
        dimensions = {"2d": 0, LIT_CUBE: 1, LIT_VOLUME: 2}
        alpha_modes = {"opaque": 0, "embedded_ldr": 1, "constant_unorm8": 2, "separate_uastc_ldr_4x4": 3}
        if values[LIT_FORMAT] not in formats or values[LIT_DIMENSION] not in dimensions or values[LIT_ALPHA_MODE] not in alpha_modes:
            raise AssertionError(f"texture metadata has an unsupported format, dimension, or alpha mode: {metadata_path}")
        dimension = dimensions[values[LIT_DIMENSION]]
        payload_format = formats[values[LIT_FORMAT]]
        alpha_mode = alpha_modes[values[LIT_ALPHA_MODE]]
        resolution = values[LIT_RESOLUTION]
        if not isinstance(resolution, list) or len(resolution) != (3 if dimension == 2 else 2) or any(type(value) is not int or value <= 0 for value in resolution):
            raise AssertionError(f"invalid texture resolution: {metadata_path}")
        width, height = resolution[:2]
        depth = resolution[2] if dimension == 2 else 1
        count = max(width, height, depth).bit_length()
        if dimension == 1 and width != height:
            raise AssertionError(f"cube texture faces must be square: {metadata_path}")
        if (alpha_mode == 2) != (LIT_ALPHA_CONSTANT_UNORM8 in values):
            raise AssertionError(f"alpha constant must appear only for constant transport: {metadata_path}")
        alpha_constant = values.get(LIT_ALPHA_CONSTANT_UNORM8, 255)
        if alpha_mode == 2 and (type(alpha_constant) is not int or not 0 <= alpha_constant <= 254):
            raise AssertionError(f"invalid alpha constant: {metadata_path}")
        if (payload_format == 0 and alpha_mode not in (0, 1)) or (payload_format == 1 and alpha_mode == 1):
            raise AssertionError(f"incompatible alpha transport: {metadata_path}")
        records = []
        mip_width, mip_height, mip_depth, primary_size = width, height, depth, 0
        for level in range(count):
            planes = 6 if dimension == 1 else mip_depth if dimension == 2 else 1
            primary_size += ((mip_width + 3) // 4) * ((mip_height + 3) // 4) * 16 * planes
            records.append((mip_width, mip_height, planes))
            terminal = mip_width == mip_height == 1 and (dimension != 2 or mip_depth == 1)
            if terminal != (level == count - 1):
                raise AssertionError(f"texture mip chain is incomplete or excessive: {metadata_path}")
            mip_width, mip_height = max(1, mip_width // 2), max(1, mip_height // 2)
            mip_depth = max(1, mip_depth // 2) if dimension == 2 else 1
        if len(path.read_bytes()) != primary_size * (2 if alpha_mode == 3 else 1):
            raise AssertionError(f"raw sidecar length does not match metadata mip and alpha layout: {path}")
        return {LIT_DIMENSION: dimension, LIT_FORMAT: payload_format, LIT_ALPHA_MODE: alpha_mode, LIT_ALPHA_CONSTANT: alpha_constant, LIT_MIPS: records}
