#!/usr/bin/env python3
"""Shared result protocol and host diagnostics for native A/B profile probes."""

from __future__ import annotations

import json
import shutil
import subprocess
from pathlib import Path
from typing import Any


class ProfileFailure(RuntimeError):
    pass


def parse_result(text: str, prefix: str) -> dict[str, Any] | None:
    for line in reversed(text.splitlines()):
        if not line.startswith(prefix):
            continue
        try:
            payload = json.loads(line[len(prefix) :])
        except json.JSONDecodeError as error:
            raise ProfileFailure(f"invalid profile result JSON: {error}") from error
        if not isinstance(payload, dict):
            raise ProfileFailure("profile result must be a JSON object")
        return payload
    return None


def capture_vulkan_summary(output_dir: Path) -> Path | None:
    vulkaninfo = shutil.which("vulkaninfo")
    if vulkaninfo is None:
        return None
    completed = subprocess.run([vulkaninfo, "--summary"], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output_path = output_dir / "vulkaninfo-summary.txt"
    output_path.write_text(completed.stdout, encoding="utf-8")
    return output_path
