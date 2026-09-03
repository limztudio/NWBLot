#!/usr/bin/env python3
"""Shared GPU-timing sidecar parsing for A/B harnesses."""

from __future__ import annotations

import re
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, Sequence

from name_symbols import known_name_symbols
from window_capture_smoke import SmokeFailure


INTERVAL_RE = re.compile(
    r"^=== interval:\s+(?P<frames>\d+)\s+frames\s+/\s+(?P<seconds>[-+0-9.eE]+)s\s+===$"
)
SCOPE_RE = re.compile(
    r"^\s{2}(?P<scope>[^:]+):\s+avg=(?P<average>[-+0-9.eE]+)"
    r"\s+min=(?P<minimum>[-+0-9.eE]+)\s+max=(?P<maximum>[-+0-9.eE]+)"
    r"\s+samples=(?P<samples>\d+)\s*$"
)


@dataclass(frozen=True)
class ScopeSummary:
    sample_count: int
    positive_sample_count: int
    median_ms: float
    mean_ms: float
    min_ms: float
    max_ms: float


def load_name_symbols(
    path: Optional[Path],
    required_scopes: Iterable[str],
    *,
    missing_file_message: str = "name-symbol sidecar does not exist: {path}",
) -> Dict[str, str]:
    decoded = known_name_symbols(tuple(required_scopes))
    if not path:
        return decoded
    if not path.is_file():
        raise SmokeFailure(missing_file_message.format(path=path))

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = raw_line.split("\t")
        if len(fields) >= 3 and fields[2]:
            decoded[fields[0]] = fields[2]
    return decoded


def parse_timing_file(
    path: Path,
    symbols: Mapping[str, str],
    start_byte_offset: int = 0,
) -> List[Dict[str, float]]:
    if not path.is_file():
        raise SmokeFailure(f"GPU timing file was not written: {path}")
    raw_timing = path.read_bytes()
    if start_byte_offset < 0 or start_byte_offset > len(raw_timing):
        raise SmokeFailure(
            f"GPU timing measurement offset {start_byte_offset} is outside {path} ({len(raw_timing)} bytes)"
        )

    intervals: List[Dict[str, float]] = []
    current: Optional[Dict[str, float]] = None
    for raw_line in raw_timing[start_byte_offset:].decode("utf-8", errors="replace").splitlines():
        if INTERVAL_RE.match(raw_line):
            if current:
                intervals.append(current)
            current = {}
            continue

        match = SCOPE_RE.match(raw_line)
        if not match or current is None:
            continue

        raw_scope = match.group("scope")
        scope = symbols.get(raw_scope, raw_scope)
        try:
            current[scope] = float(match.group("average"))
        except ValueError as error:
            raise SmokeFailure(f"invalid GPU timing line in {path}: {raw_line}") from error

    if current:
        intervals.append(current)
    return intervals


def summarize_samples(values: Sequence[float]) -> ScopeSummary:
    if not values:
        return ScopeSummary(0, 0, 0.0, 0.0, 0.0, 0.0)
    return ScopeSummary(
        sample_count=len(values),
        positive_sample_count=sum(value > 1.0e-6 for value in values),
        median_ms=statistics.median(values),
        mean_ms=statistics.fmean(values),
        min_ms=min(values),
        max_ms=max(values),
    )


def summarize_scopes(intervals: Iterable[Mapping[str, float]]) -> Dict[str, ScopeSummary]:
    samples: Dict[str, List[float]] = {}
    for interval in intervals:
        for scope, value in interval.items():
            samples.setdefault(scope, []).append(value)
    return {scope: summarize_samples(scope_values) for scope, scope_values in sorted(samples.items())}


def require_scope_samples(
    summaries: Mapping[str, ScopeSummary],
    scope: str,
    minimum_samples: int,
    timing_file: Path,
) -> ScopeSummary:
    summary = summaries.get(scope)
    if summary and summary.sample_count >= minimum_samples:
        return summary

    observed = ", ".join(summaries) or "none"
    raise SmokeFailure(
        f"required timing scope '{scope}' has fewer than {minimum_samples} samples in {timing_file}; "
        f"observed scopes: {observed}. Build dbg/namesym or pass the matching --*-namesym sidecar."
    )
