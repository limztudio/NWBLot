#!/usr/bin/env python3
"""Immutable launcher data models (settings, targets, sessions)."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple


@dataclass(frozen=True)
class LaunchSettings:
    root: Path
    platform_name: str
    arch: str
    domain: str
    config: str
    configure_preset: str
    build_dir: Path
    cmake: Tuple[str, ...]


@dataclass(frozen=True)
class CMakeTargetInfo:
    name: str
    target_type: str
    artifacts: Tuple[Path, ...]


@dataclass(frozen=True)
class RepoLauncher:
    command: str
    script: Path
    route: Tuple[Path, ...] = ()


@dataclass(frozen=True)
class ProfileSession:
    log_port: int
    logserver_executable: Path
    process: Optional[subprocess.Popen]
