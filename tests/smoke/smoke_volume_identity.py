#!/usr/bin/env python3
"""Identify authored packed resources while separating the exact backend-owned mutable pipeline cache."""

import hashlib


def volume_segment_filename(volume_name, segment_index):
    """Match global/filesystem/volume_naming.h: UTF-8 FNV-1a of the canonical name_index.vol."""
    value = 14695981039346656037
    for byte in f"{volume_name}_{segment_index}.vol".encode("utf-8"):
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}.vol"


def runtime_pipeline_cache_paths(directory):
    resources = directory / "res"
    if not resources.is_dir():
        return []
    result = []
    # Packed-volume segments are contiguous from zero. A missing earlier segment cannot excuse an unrelated file.
    for index in range(sum(1 for path in resources.iterdir() if path.is_file()) + 1):
        path = resources / volume_segment_filename("runtime_pipeline_cache", index)
        if not path.is_file():
            break
        result.append(path)
    return result


def file_identity(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return {"bytes": path.stat().st_size, "sha256": digest.hexdigest()}


def authored_volume_hashes(directory):
    mutable = set(runtime_pipeline_cache_paths(directory))
    return {path.relative_to(directory).as_posix(): file_identity(path)["sha256"]
        for path in sorted((directory / "res").rglob("*.vol")) if path.is_file() and path not in mutable}
