#!/usr/bin/env python3
"""Helpers for decoding stable engine Name hash tokens in A/B timing artifacts."""

from __future__ import annotations

from typing import Dict, Iterable


FNV64_OFFSET_BASIS = 14695981039346656037
FNV64_PRIME = 1099511628211
UINT64_MASK = (1 << 64) - 1
NAME_HASH_LANE_SEEDS = (
    FNV64_OFFSET_BASIS,
    FNV64_OFFSET_BASIS ^ 0x0123456789ABCDEF,
    FNV64_OFFSET_BASIS ^ 0xFEDCBA9876543210,
    FNV64_OFFSET_BASIS ^ 0x0F1E2D3C4B5A6978,
    FNV64_OFFSET_BASIS ^ 0x8796A5B4C3D2E1F0,
    FNV64_OFFSET_BASIS ^ 0xDEADBEEFCAFEBABE,
    FNV64_OFFSET_BASIS ^ 0x1234ABCD5678EF01,
    FNV64_OFFSET_BASIS ^ 0xA0B1C2D3E4F50617,
)


def debug_name_hash_token(text: str) -> str:
    """Match global/name.h's canonical eight-lane FNV-1a debug token."""
    lanes = list(NAME_HASH_LANE_SEEDS)
    for byte in text.encode("utf-8"):
        if ord("A") <= byte <= ord("Z"):
            byte += ord("a") - ord("A")
        elif byte == ord("\\"):
            byte = ord("/")
        for index, lane in enumerate(lanes):
            lanes[index] = ((lane ^ byte) * FNV64_PRIME) & UINT64_MASK
    return "_".join(f"{lane:016x}" for lane in lanes)


def known_name_symbols(names: Iterable[str]) -> Dict[str, str]:
    return {debug_name_hash_token(name): name for name in names}
