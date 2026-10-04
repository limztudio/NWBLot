#!/usr/bin/env python3
"""Pool and encode surviving Clang literal globals without changing named objects."""

from __future__ import annotations

import ctypes
import hashlib
import os
import struct
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import Dict, Optional, Tuple

from launcher.string_literals.llvm_api import LlvmApi, REF, SIZE


RECORD_MAGIC = 0x474C4253
RECORD_HEADER_BYTES = 24
RECORD_MIN_ALIGNMENT = 16
RECORD_NAME_PREFIX = "__glb_literal_"
INTEGER_TYPE = 8
ARRAY_TYPE = 11
GLOBAL_UNNAMED_ADDRESS = 2
ELIGIBLE_LINKAGES = frozenset((3, 8, 9))
WEAK_ODR_LINKAGE = 6
HIDDEN_VISIBILITY = 1
MAX_RECORD_BYTES = 0xFFFFFFFF


def _target_format(triple: str) -> Tuple[str, bytes]:
    parts = triple.lower().split("-")
    if "windows" in parts or "win32" in parts:
        return "coff", b".glb$M"
    if any(part.startswith(("darwin", "macos", "ios", "tvos", "watchos")) for part in parts):
        return "macho", b"__DATA,__glblit"
    if any(part in ("linux", "freebsd", "netbsd", "openbsd", "android") for part in parts):
        return "elf", b"glb_literals"
    raise ValueError(f"Unsupported string-literal target triple: {triple}")


def _literal_name(name: str) -> bool:
    return name.startswith((".str", ".L.str", "??_C@"))


def _retained_globals(api: LlvmApi, module: int) -> set:
    retained = set()
    for name in (b"llvm.used", b"llvm.compiler.used"):
        global_value = api.LLVMGetNamedGlobal(module, name)
        if not global_value:
            continue
        pending = [api.LLVMGetInitializer(global_value)]
        visited = set()
        while pending:
            value = pending.pop()
            if not value or value in visited:
                continue
            visited.add(value)
            if api.LLVMIsAGlobalVariable(value):
                retained.add(value)
            elif api.LLVMIsAConstantArray(value) or api.LLVMIsAConstantStruct(value) or api.LLVMIsAConstantExpr(value):
                pending.extend(api.LLVMGetOperand(value, index) for index in range(api.LLVMGetNumOperands(value)))
    return retained


def _byte_order(data: bytes, width: int, source_order: str, target_order: str) -> bytes:
    if width == 1 or source_order == target_order:
        return data
    return b"".join(data[index:index + width][::-1] for index in range(0, len(data), width))


def _array_bytes(api: LlvmApi, initializer: int, count: int, width: int, target_order: str) -> Optional[bytes]:
    if api.LLVMIsNull(initializer):
        return bytes(count * width)
    if api.LLVMIsAConstantDataArray(initializer):
        length = SIZE()
        pointer = api.LLVMGetRawDataValues(initializer, ctypes.byref(length))
        if length.value != count * width:
            raise RuntimeError("LLVM constant data has an inconsistent byte count")
        return _byte_order(ctypes.string_at(pointer, length.value), width, sys.byteorder, target_order)
    if api.LLVMIsAConstantArray(initializer):
        values = []
        for index in range(count):
            element = api.LLVMGetAggregateElement(initializer, index)
            if not element or not api.LLVMIsAConstantInt(element):
                return None
            values.append(api.LLVMConstIntGetZExtValue(element).to_bytes(width, target_order))
        return b"".join(values)
    return None


def _candidate(api: LlvmApi, target_data: int, value: int, target_order: str) -> Tuple[Optional[dict], str]:
    array_type = api.LLVMGlobalGetValueType(value)
    if api.LLVMGetTypeKind(array_type) != ARRAY_TYPE:
        return None, "non_array"
    element_type = api.LLVMGetElementType(array_type)
    if api.LLVMGetTypeKind(element_type) != INTEGER_TYPE:
        return None, "non_character_array"
    bits = api.LLVMGetIntTypeWidth(element_type)
    if bits not in (8, 16, 32):
        return None, "non_character_array"
    name = api.value_name(value)
    if not _literal_name(name):
        return None, "named_array"
    if not api.LLVMIsGlobalConstant(value):
        return None, "mutable_literal"
    if api.LLVMGetUnnamedAddress(value) != GLOBAL_UNNAMED_ADDRESS:
        return None, "significant_address"
    if api.LLVMGetLinkage(value) not in ELIGIBLE_LINKAGES or api.LLVMGetDLLStorageClass(value):
        return None, "external_identity"
    if api.LLVMGetThreadLocalMode(value) or api.LLVMIsExternallyInitialized(value):
        return None, "external_initialization"
    if api.LLVMGetPointerAddressSpace(api.LLVMTypeOf(value)):
        return None, "non_default_address_space"
    if api.LLVMGetSection(value):
        return None, "explicit_section"
    initializer = api.LLVMGetInitializer(value)
    if not initializer:
        return None, "declaration"
    count = api.LLVMGetArrayLength2(array_type)
    width = bits // 8
    if not count:
        return None, "empty_array"
    if count * width > MAX_RECORD_BYTES:
        raise ValueError("Literal byte count exceeds the current record contract")
    raw = _array_bytes(api, initializer, count, width, target_order)
    if raw is None:
        return None, "non_data_initializer"
    if raw[-width:] != bytes(width):
        return None, "not_nul_terminated"
    alignment = max(api.LLVMGetAlignment(value), api.LLVMABIAlignmentOfType(target_data, array_type), RECORD_MIN_ALIGNMENT)
    if alignment & (alignment - 1):
        raise ValueError("Literal alignment is not a power of two")
    return {"value": value, "type": array_type, "element_type": element_type, "raw": raw, "width": width, "alignment": alignment}, ""


def _record(api: LlvmApi, context: int, module: int, target_data: int, candidate: dict, image_format: str, section: bytes) -> Tuple[int, dict]:
    raw = candidate["raw"]
    width = candidate["width"]
    alignment = candidate["alignment"]
    digest = hashlib.sha256(raw + struct.pack("<II", width, alignment)).digest()
    name = RECORD_NAME_PREFIX + digest.hex()
    seed = int.from_bytes(digest[:8], "little") or 1
    payload_offset = (RECORD_HEADER_BYTES + alignment - 1) & -alignment
    stride = (payload_offset + len(raw) + alignment - 1) & -alignment
    if stride > MAX_RECORD_BYTES:
        raise ValueError("Literal record stride exceeds the current record contract")
    info = {"name": name, "byte_count": len(raw), "payload_offset": payload_offset, "record_stride": stride, "alignment": alignment, "width": width, "seed": seed}
    record = api.LLVMGetNamedGlobal(module, name.encode("ascii"))
    if record:
        raise RuntimeError(f"Input module already defines reserved literal record {name}")
    encoded = bytes(value ^ (((seed >> ((index & 7) * 8)) ^ (index * 0x9D + (index >> 8))) & 255) for index, value in enumerate(raw))
    host_encoded = _byte_order(encoded, width, candidate["target_order"], sys.byteorder)
    i8 = api.LLVMIntTypeInContext(context, 8)
    i32 = api.LLVMIntTypeInContext(context, 32)
    i64 = api.LLVMIntTypeInContext(context, 64)
    prefix_type = api.LLVMArrayType2(i8, payload_offset - RECORD_HEADER_BYTES)
    tail_type = api.LLVMArrayType2(i8, stride - payload_offset - len(raw))
    fields = (REF * 8)(i32, i32, i32, i32, i64, prefix_type, candidate["type"], tail_type)
    record_type = api.LLVMStructTypeInContext(context, fields, len(fields), 1)
    if api.LLVMABISizeOfType(target_data, record_type) != stride:
        raise RuntimeError("Packed LLVM record layout differs from its declared stride")
    values = (REF * 8)(
        api.LLVMConstInt(i32, RECORD_MAGIC, 0),
        api.LLVMConstInt(i32, len(raw), 0),
        api.LLVMConstInt(i32, payload_offset, 0),
        api.LLVMConstInt(i32, stride, 0),
        api.LLVMConstInt(i64, seed, 0),
        api.LLVMConstNull(prefix_type),
        api.LLVMConstDataArray(candidate["element_type"], host_encoded, len(host_encoded)),
        api.LLVMConstNull(tail_type),
    )
    record = api.LLVMAddGlobal(module, record_type, name.encode("ascii"))
    api.LLVMSetInitializer(record, api.LLVMConstStructInContext(context, values, len(values), 1))
    api.LLVMSetGlobalConstant(record, 0)
    api.LLVMSetLinkage(record, WEAK_ODR_LINKAGE)
    api.LLVMSetVisibility(record, HIDDEN_VISIBILITY)
    api.LLVMSetAlignment(record, alignment)
    api.LLVMSetUnnamedAddress(record, GLOBAL_UNNAMED_ADDRESS)
    api.LLVMSetSection(record, section)
    if image_format != "macho":
        comdat = api.LLVMGetOrInsertComdat(module, name.encode("ascii"))
        api.LLVMSetComdatSelectionKind(comdat, 0)
        api.LLVMSetComdat(record, comdat)
    indices = (REF * 2)(api.LLVMConstInt(i32, 0, 0), api.LLVMConstInt(i32, 6, 0))
    payload = api.LLVMConstInBoundsGEP2(record_type, record, indices, len(indices))
    return payload, info


def transform_bitcode(input_path: Path, output_path: Path, llvm_library: Path, expected_version: Tuple[int, int, int]) -> Dict[str, object]:
    """Rewrite optimized bitcode; output appears only after successful verification."""
    input_path = Path(input_path).resolve(strict=True)
    output_path = Path(output_path).resolve()
    api = LlvmApi(llvm_library, expected_version)
    context = api.LLVMContextCreate()
    module = None
    target_data = None
    temporary_path = None
    try:
        module = api.read_module(context, input_path)
        api.verify_module(module)
        triple = (api.LLVMGetTarget(module) or b"").decode("ascii")
        image_format, section = _target_format(triple)
        layout = api.LLVMGetDataLayoutStr(module)
        if not layout:
            raise ValueError("Literal transformation requires an explicit target data layout")
        target_data = api.LLVMCreateTargetData(layout)
        target_order = "little" if api.LLVMByteOrder(target_data) else "big"
        globals_to_visit = []
        value = api.LLVMGetFirstGlobal(module)
        while value:
            globals_to_visit.append(value)
            value = api.LLVMGetNextGlobal(value)
        skipped = Counter()
        retained = _retained_globals(api, module)
        pooled = {}
        records = []
        transformed = 0
        raw_bytes = 0
        for value in globals_to_visit:
            if value in retained:
                skipped["explicit_retention"] += 1
                continue
            candidate, reason = _candidate(api, target_data, value, target_order)
            if candidate is None:
                skipped[reason] += 1
                continue
            candidate["target_order"] = target_order
            key = (candidate["raw"], candidate["width"], candidate["alignment"])
            payload = pooled.get(key)
            if payload is None:
                payload, info = _record(api, context, module, target_data, candidate, image_format, section)
                pooled[key] = payload
                records.append(info)
            api.LLVMReplaceAllUsesWith(value, payload)
            api.LLVMDeleteGlobal(value)
            transformed += 1
            raw_bytes += len(candidate["raw"])
        api.verify_module(module)
        descriptor, temporary_name = tempfile.mkstemp(prefix=output_path.name + ".", suffix=".tmp", dir=output_path.parent)
        os.close(descriptor)
        temporary_path = Path(temporary_name).resolve()
        if temporary_path.parent != output_path.parent:
            raise RuntimeError("Temporary bitcode output escaped its destination directory")
        if api.LLVMWriteBitcodeToFile(module, os.fsencode(temporary_path)):
            raise OSError(f"LLVM cannot write transformed bitcode {output_path}")
        os.replace(temporary_path, output_path)
        temporary_path = None
        return {
            "llvm_version": api.version,
            "target": triple,
            "format": image_format,
            "section": section.decode("ascii"),
            "byte_order": target_order,
            "transformed_globals": transformed,
            "pooled_records": len(records),
            "raw_literal_bytes": raw_bytes,
            "record_bytes": sum(record["record_stride"] for record in records),
            "skipped_globals": dict(sorted(skipped.items())),
            "records": records,
        }
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
        if target_data:
            api.LLVMDisposeTargetData(target_data)
        if module:
            api.LLVMDisposeModule(module)
        api.LLVMContextDispose(context)

