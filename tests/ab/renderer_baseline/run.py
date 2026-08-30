#!/usr/bin/env python3
"""Capture an immutable renderer baseline or compare one candidate against it.

The workflow deliberately lives entirely under ``tests/``.  It takes no renderer
feature switch: a baseline is an image and a manifest from a known source revision,
while a candidate is the normal selected smoke executable at a later revision.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace
from typing import Dict, List, Mapping, Optional, Sequence, Tuple


REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from profiles import BaselineProfile, get_profile, profile_names  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    SKIP_EXIT_CODE,
    STRICT_LOG_FAILURE_MESSAGES,
    SmokeFailure,
    SmokeSkip,
    build_launch_environment,
    collect_log_delta,
    create_capture_backend,
    ensure_process_running,
    launch_logserver,
    launch_testbed,
    require_normal_testbed_exit,
    terminate_process,
    validate_capture_result,
    wait_for_log_drain,
    wait_for_log_message,
)


SCHEMA = "nwb.renderer-baseline.v1"
CORPUS_SCHEMA = "nwb.renderer-baseline-corpus.v1"
CURRENT_CORPUS_ID = "current-renderer-v1"
CURRENT_CORPUS_FILE = Path(__file__).with_name("current_renderer_corpus.json")
FORBIDDEN_LOG_MESSAGES = (
    *STRICT_LOG_FAILURE_MESSAGES,
    "cannot safely continue after an unresolved frame recovery submission",
)


@dataclass(frozen=True)
class PixelDifference:
    width: int
    height: int
    max_abs: int
    mean_abs: float
    changed_pixels: int
    total_pixels: int

    @property
    def changed_fraction(self) -> float:
        return self.changed_pixels / self.total_pixels if self.total_pixels else 0.0


@dataclass(frozen=True)
class CaptureResult:
    capture_file: str
    log_file: str
    capture_sha256: str
    executable_sha256: str
    source_revision: str
    source_worktree_clean: bool
    frozen_environment: Mapping[str, str]
    forbidden_log_messages: Tuple[str, ...]


@dataclass(frozen=True)
class CorpusReference:
    corpus_id: str
    reference_directory: Path
    profile_entry: Mapping[str, object]
    limits: Mapping[str, float]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
    except OSError as error:
        raise SmokeFailure(f"could not hash '{path}': {error}") from error
    return digest.hexdigest()


def source_revision() -> str:
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=REPO,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return "unavailable"
    return result.stdout.strip() if result.returncode == 0 and result.stdout.strip() else "unavailable"


def source_worktree_clean() -> bool:
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=REPO,
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return False
    return result.returncode == 0 and not result.stdout.strip()


def effective_frozen_environment(profile: BaselineProfile) -> Dict[str, str]:
    return {
        name: os.environ.get(name, default_value)
        for name, default_value in sorted(profile.frozen_environment.items())
    }


def canonical_frozen_environment(profile: BaselineProfile) -> Dict[str, str]:
    return dict(sorted(profile.frozen_environment.items()))


def capture_mode(profile: BaselineProfile) -> str:
    return "frame-locked" if profile.capture_freeze_frame != 0 else "settled"


def read_bmp_rgb(path: Path) -> Tuple[int, int, bytes]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise SmokeFailure(f"could not read BMP '{path}': {error}") from error
    if len(data) < 54 or data[:2] != b"BM":
        raise SmokeFailure(f"capture is not a BMP: {path}")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40 or len(data) < 14 + dib_size:
        raise SmokeFailure(f"capture has an unsupported DIB header: {path}")

    width, signed_height, planes, bits_per_pixel, compression = struct.unpack_from("<iiHHI", data, 18)
    if width <= 0 or signed_height == 0 or planes != 1 or bits_per_pixel != 24 or compression != 0:
        raise SmokeFailure(f"capture must be an uncompressed 24-bit BMP: {path}")

    height = abs(signed_height)
    source_stride = ((width * 3 + 3) // 4) * 4
    if pixel_offset + source_stride * height > len(data):
        raise SmokeFailure(f"capture pixel data is truncated: {path}")

    rows: List[bytes] = []
    bottom_up = signed_height > 0
    for logical_row in range(height):
        source_row = height - 1 - logical_row if bottom_up else logical_row
        source_offset = pixel_offset + source_row * source_stride
        bgr = data[source_offset:source_offset + width * 3]
        rgb = bytearray(width * 3)
        for pixel in range(0, len(bgr), 3):
            rgb[pixel] = bgr[pixel + 2]
            rgb[pixel + 1] = bgr[pixel + 1]
            rgb[pixel + 2] = bgr[pixel]
        rows.append(bytes(rgb))
    return width, height, b"".join(rows)


def write_bmp_rgb(path: Path, width: int, height: int, rgb: bytes) -> None:
    if width <= 0 or height <= 0 or len(rgb) != width * height * 3:
        raise ValueError("invalid RGB baseline image dimensions or byte count")

    path.parent.mkdir(parents=True, exist_ok=True)
    row_stride = ((width * 3 + 3) // 4) * 4
    image_size = row_stride * height
    padding = b"\0" * (row_stride - width * 3)
    with path.open("wb") as output:
        output.write(struct.pack("<2sIHHI", b"BM", 14 + 40 + image_size, 0, 0, 54))
        output.write(struct.pack("<IIIHHIIIIII", 40, width, height, 1, 24, 0, image_size, 0, 0, 0, 0))
        for row_index in range(height - 1, -1, -1):
            row = rgb[row_index * width * 3:(row_index + 1) * width * 3]
            bgr = bytearray(width * 3)
            for pixel in range(0, len(row), 3):
                bgr[pixel] = row[pixel + 2]
                bgr[pixel + 1] = row[pixel + 1]
                bgr[pixel + 2] = row[pixel]
            output.write(bgr)
            output.write(padding)


def compare_bmp_rgb(reference_path: Path, candidate_path: Path, difference_path: Path) -> PixelDifference:
    reference_width, reference_height, reference_rgb = read_bmp_rgb(reference_path)
    candidate_width, candidate_height, candidate_rgb = read_bmp_rgb(candidate_path)
    if (reference_width, reference_height) != (candidate_width, candidate_height):
        raise SmokeFailure(
            "reference and candidate captures have different dimensions: "
            f"{reference_width}x{reference_height} vs {candidate_width}x{candidate_height}"
        )

    total_abs = 0
    max_abs = 0
    changed_pixels = 0
    difference_rgb = bytearray(len(reference_rgb))
    for offset in range(0, len(reference_rgb), 3):
        changed = False
        for channel in range(3):
            delta = abs(reference_rgb[offset + channel] - candidate_rgb[offset + channel])
            total_abs += delta
            max_abs = max(max_abs, delta)
            difference_rgb[offset + channel] = min(255, delta * 8)
            changed = changed or delta != 0
        if changed:
            changed_pixels += 1

    write_bmp_rgb(difference_path, reference_width, reference_height, bytes(difference_rgb))
    total_pixels = reference_width * reference_height
    return PixelDifference(
        width=reference_width,
        height=reference_height,
        max_abs=max_abs,
        mean_abs=total_abs / len(reference_rgb),
        changed_pixels=changed_pixels,
        total_pixels=total_pixels,
    )


def difference_failures(args: argparse.Namespace, difference: PixelDifference) -> List[str]:
    maximum_max_abs = 0 if args.require_exact else args.maximum_max_abs
    maximum_mean_abs = 0.0 if args.require_exact else args.maximum_mean_abs
    maximum_changed_fraction = 0.0 if args.require_exact else args.maximum_changed_fraction
    failures: List[str] = []
    if maximum_max_abs is not None and difference.max_abs > maximum_max_abs:
        failures.append(f"max abs {difference.max_abs} exceeds {maximum_max_abs}")
    if maximum_mean_abs is not None and difference.mean_abs > maximum_mean_abs:
        failures.append(f"mean abs {difference.mean_abs:.6f} exceeds {maximum_mean_abs:.6f}")
    if maximum_changed_fraction is not None and difference.changed_fraction > maximum_changed_fraction:
        failures.append(
            f"changed fraction {difference.changed_fraction:.6f} exceeds {maximum_changed_fraction:.6f}"
        )
    return failures


def make_launch_args(args: argparse.Namespace) -> SimpleNamespace:
    return SimpleNamespace(
        no_logserver=args.no_logserver,
        logserver_executable=args.logserver_executable,
        log_port=0,
        working_directory=args.runtime_dir,
        timeout=args.startup_timeout,
        application_arg=["--gpudbg"] if args.gpu_validation else [],
        software_vulkan="off",
    )


def capture_scene(
    args: argparse.Namespace,
    profile: BaselineProfile,
    capture_path: Path,
    log_path: Path,
    frozen_environment: Mapping[str, str],
) -> CaptureResult:
    if not args.executable.is_file():
        raise SmokeFailure(f"renderer baseline executable does not exist: {args.executable}")
    if not args.runtime_dir.is_dir():
        raise SmokeFailure(f"renderer baseline runtime directory does not exist: {args.runtime_dir}")

    launch_args = make_launch_args(args)
    environment = build_launch_environment(launch_args)
    environment["NWB_RENDER_UNFOCUSED"] = "1"
    environment.update(frozen_environment)
    if profile.capture_freeze_frame != 0:
        environment["NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"] = str(profile.capture_freeze_frame)
    if profile.fixed_delta_seconds != 0.0:
        environment["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = f"{profile.fixed_delta_seconds:.9g}"
    backend = None
    logserver_process = None
    app_process = None
    window = None
    app_exit_code = None
    app_exit_tail = ""
    log_directory: Optional[Path] = None
    log_baseline: Mapping[Path, int] = {}
    log_pattern = ""
    log_text = ""
    try:
        backend = create_capture_backend()
        logserver_process, log_port, log_directory, log_baseline, log_pattern = launch_logserver(
            launch_args, args.executable, environment
        )
        app_process = launch_testbed(launch_args, args.executable, environment, log_port)
        window = backend.wait_for_window(app_process.pid, args.startup_timeout, profile.window_title)
        if not window:
            ensure_process_running(app_process, f"while waiting for '{profile.window_title}'")
            raise SmokeFailure(f"renderer baseline did not expose expected window '{profile.window_title}'")
        if profile.capture_freeze_frame != 0:
            if not profile.capture_ready_log:
                raise SmokeFailure(f"frame-locked profile '{args.profile}' has no capture-ready log marker")
            if not log_directory:
                raise SmokeFailure("frame-locked renderer baseline requires a readable runtime-log directory")
            wait_for_log_message(
                log_directory,
                log_baseline,
                log_pattern,
                profile.capture_ready_log,
                args.startup_timeout,
            )
            # Submission is already suspended, so this wait allows the final accepted present to become visible
            # without advancing the frame-locked temporal state.
            time.sleep(args.settle_seconds)
        else:
            time.sleep(args.settle_seconds)
        ensure_process_running(app_process, "before baseline capture")
        validate_capture_result(backend.capture_window(window, capture_path))
    finally:
        if app_process is not None:
            app_exit_code, app_exit_tail = terminate_process(app_process, "renderer baseline capture", window)
        if log_directory:
            wait_for_log_drain(log_directory, log_baseline, log_pattern)
            log_text = collect_log_delta(log_directory, log_baseline, log_pattern)
        terminate_process(logserver_process, "renderer baseline logserver")
        if backend:
            backend.close()

    require_normal_testbed_exit(app_exit_code, app_exit_tail)
    if not capture_path.is_file():
        raise SmokeFailure(f"renderer baseline did not create capture: {capture_path}")
    forbidden = tuple(message for message in FORBIDDEN_LOG_MESSAGES if message in log_text)
    forbidden += tuple(message for message in args.reject_log if message in log_text)
    if forbidden:
        raise SmokeFailure(f"renderer baseline capture found forbidden log messages: {list(forbidden)}")
    log_path.write_text(log_text, encoding="utf-8")
    return CaptureResult(
        capture_file=capture_path.name,
        log_file=log_path.name,
        capture_sha256=sha256_file(capture_path),
        executable_sha256=sha256_file(args.executable),
        source_revision=source_revision(),
        source_worktree_clean=source_worktree_clean(),
        frozen_environment=dict(frozen_environment),
        forbidden_log_messages=forbidden,
    )


def manifest_payload(
    capture_kind: str,
    profile_name: str,
    profile: BaselineProfile,
    args: argparse.Namespace,
    capture: CaptureResult,
    reference_manifest: Optional[Mapping[str, object]] = None,
) -> Dict[str, object]:
    payload: Dict[str, object] = {
        "schema": SCHEMA,
        "capture_kind": capture_kind,
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "profile": profile_name,
        "profile_description": profile.description,
        "target": profile.target,
        "window_title": profile.window_title,
        "capture_mode": capture_mode(profile),
        "capture_freeze_frame": profile.capture_freeze_frame,
        "fixed_delta_seconds": profile.fixed_delta_seconds,
        "settle_seconds": args.settle_seconds,
        "gpu_validation": args.gpu_validation,
        "runtime_directory": str(args.runtime_dir),
        "source_revision": capture.source_revision,
        "source_worktree_clean": capture.source_worktree_clean,
        "executable_sha256": capture.executable_sha256,
        "capture_file": capture.capture_file,
        "capture_sha256": capture.capture_sha256,
        "log_file": capture.log_file,
        "frozen_environment": dict(capture.frozen_environment),
        "platform": platform.platform(),
        "python_version": platform.python_version(),
    }
    if reference_manifest is not None:
        payload["reference_source_revision"] = reference_manifest.get("source_revision")
        payload["reference_capture_sha256"] = reference_manifest.get("capture_sha256")
    return payload


def write_json(path: Path, payload: Mapping[str, object]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_reference(
    reference_directory: Path,
    profile_name: str,
    frozen_environment: Mapping[str, str],
    settle_seconds: float,
    gpu_validation: bool,
    capture_freeze_frame: int,
    fixed_delta_seconds: float,
) -> Tuple[Mapping[str, object], Path]:
    manifest_path = reference_directory / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise SmokeFailure(f"could not read baseline manifest '{manifest_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"baseline manifest is not valid JSON: {manifest_path}: {error}") from error
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise SmokeFailure(f"baseline manifest has an unsupported schema: {manifest_path}")
    if manifest.get("capture_kind") != "baseline":
        raise SmokeFailure(f"reference manifest is not an immutable baseline: {manifest_path}")
    if manifest.get("profile") != profile_name:
        raise SmokeFailure(
            f"baseline profile mismatch: reference is {manifest.get('profile')!r}, candidate is {profile_name!r}"
        )
    if manifest.get("frozen_environment") != dict(frozen_environment):
        raise SmokeFailure("baseline frozen environment differs from the candidate capture")
    if manifest.get("gpu_validation") != gpu_validation:
        raise SmokeFailure("baseline GPU-validation mode differs from the candidate capture")
    if manifest.get("settle_seconds") != settle_seconds:
        raise SmokeFailure("baseline settle duration differs from the candidate capture")
    expected_capture_mode = "frame-locked" if capture_freeze_frame != 0 else "settled"
    if manifest.get("capture_mode") != expected_capture_mode:
        raise SmokeFailure("baseline capture mode differs from the candidate capture")
    if manifest.get("capture_freeze_frame") != capture_freeze_frame:
        raise SmokeFailure("baseline capture freeze frame differs from the candidate capture")
    if manifest.get("fixed_delta_seconds") != fixed_delta_seconds:
        raise SmokeFailure("baseline fixed simulation delta differs from the candidate capture")
    capture_name = manifest.get("capture_file")
    if not isinstance(capture_name, str) or not capture_name:
        raise SmokeFailure(f"baseline manifest has no capture filename: {manifest_path}")
    capture_path = reference_directory / capture_name
    if not capture_path.is_file():
        raise SmokeFailure(f"baseline capture does not exist: {capture_path}")
    expected_hash = manifest.get("capture_sha256")
    if not isinstance(expected_hash, str) or sha256_file(capture_path) != expected_hash:
        raise SmokeFailure(f"baseline capture checksum does not match its immutable manifest: {capture_path}")
    return manifest, capture_path


def load_corpus(corpus_id: str, corpus_path: Path = CURRENT_CORPUS_FILE) -> Mapping[str, object]:
    try:
        corpus = json.loads(corpus_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise SmokeFailure(f"could not read renderer baseline corpus '{corpus_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"renderer baseline corpus is not valid JSON: {corpus_path}: {error}") from error
    if not isinstance(corpus, dict) or corpus.get("schema") != CORPUS_SCHEMA:
        raise SmokeFailure(f"renderer baseline corpus has an unsupported schema: {corpus_path}")
    if corpus.get("corpus_id") != corpus_id:
        raise SmokeFailure(f"renderer baseline corpus id does not match '{corpus_id}': {corpus_path}")
    if not isinstance(corpus.get("artifact_root"), str):
        raise SmokeFailure(f"renderer baseline corpus has no artifact root: {corpus_path}")
    if not isinstance(corpus.get("profiles"), dict):
        raise SmokeFailure(f"renderer baseline corpus has no profile table: {corpus_path}")
    return corpus


def corpus_artifact_root(corpus: Mapping[str, object], supplied_root: Optional[Path]) -> Path:
    if supplied_root is not None:
        return supplied_root
    artifact_root = corpus.get("artifact_root")
    assert isinstance(artifact_root, str)
    return REPO / artifact_root


def corpus_child(root: Path, relative_path: str, description: str) -> Path:
    relative = Path(relative_path)
    if relative.is_absolute():
        raise SmokeFailure(f"renderer baseline corpus {description} must be relative: {relative_path}")
    resolved_root = root.resolve()
    resolved_child = (resolved_root / relative).resolve()
    try:
        resolved_child.relative_to(resolved_root)
    except ValueError as error:
        raise SmokeFailure(f"renderer baseline corpus {description} escapes its artifact root: {relative_path}") from error
    return resolved_child


def corpus_string(entry: Mapping[str, object], key: str, profile_name: str) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no valid {key}")
    return value


def corpus_number(entry: Mapping[str, object], key: str, profile_name: str) -> float:
    value = entry.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no valid {key}")
    return float(value)


def load_corpus_reference(
    corpus_id: str,
    supplied_root: Optional[Path],
    profile_name: str,
    profile: BaselineProfile,
    gpu_validation: bool,
    frozen_environment: Optional[Mapping[str, str]] = None,
    corpus_path: Path = CURRENT_CORPUS_FILE,
) -> Tuple[Mapping[str, object], Path, CorpusReference]:
    corpus = load_corpus(corpus_id, corpus_path)
    capture_environment = corpus.get("capture_environment")
    if not isinstance(capture_environment, dict):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no capture environment")
    expected_gpu_validation = capture_environment.get("gpu_validation")
    if not isinstance(expected_gpu_validation, bool):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no GPU-validation mode")
    if gpu_validation != expected_gpu_validation:
        raise SmokeFailure(
            f"renderer baseline corpus '{corpus_id}' requires gpu_validation={expected_gpu_validation}, "
            f"got {gpu_validation}"
        )

    profiles = corpus.get("profiles")
    assert isinstance(profiles, dict)
    entry = profiles.get(profile_name)
    if not isinstance(entry, dict):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no profile '{profile_name}'")
    reference = entry.get("reference")
    if not isinstance(reference, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no reference")
    artifact_root = corpus_artifact_root(corpus, supplied_root)
    reference_directory = corpus_child(
        artifact_root,
        corpus_string(reference, "directory", profile_name),
        f"reference directory for '{profile_name}'",
    )
    manifest_path = reference_directory / "manifest.json"
    log_path = reference_directory / "runtime.log"
    if sha256_file(manifest_path) != corpus_string(reference, "manifest_sha256", profile_name):
        raise SmokeFailure(f"renderer baseline corpus manifest checksum does not match for '{profile_name}'")
    if sha256_file(log_path) != corpus_string(reference, "runtime_log_sha256", profile_name):
        raise SmokeFailure(f"renderer baseline corpus runtime-log checksum does not match for '{profile_name}'")

    manifest, capture = load_reference(
        reference_directory,
        profile_name,
        effective_frozen_environment(profile) if frozen_environment is None else frozen_environment,
        profile.settle_seconds,
        gpu_validation,
        profile.capture_freeze_frame,
        profile.fixed_delta_seconds,
    )
    if manifest.get("source_revision") != corpus_string(reference, "source_revision", profile_name):
        raise SmokeFailure(f"renderer baseline corpus source revision does not match for '{profile_name}'")
    if manifest.get("capture_sha256") != corpus_string(reference, "capture_sha256", profile_name):
        raise SmokeFailure(f"renderer baseline corpus capture manifest does not match for '{profile_name}'")
    if sha256_file(capture) != corpus_string(reference, "capture_sha256", profile_name):
        raise SmokeFailure(f"renderer baseline corpus image checksum does not match for '{profile_name}'")

    limits_entry = entry.get("limits")
    if not isinstance(limits_entry, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no comparison limits")
    limits = {
        "maximum_max_abs": corpus_number(limits_entry, "maximum_max_abs", profile_name),
        "maximum_mean_abs": corpus_number(limits_entry, "maximum_mean_abs", profile_name),
        "maximum_changed_fraction": corpus_number(limits_entry, "maximum_changed_fraction", profile_name),
    }
    if limits["maximum_max_abs"] < 0.0 or limits["maximum_mean_abs"] < 0.0:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has negative comparison limits")
    if not 0.0 <= limits["maximum_changed_fraction"] <= 1.0:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has an invalid changed-fraction limit")
    recapture = entry.get("same_revision_recapture")
    if not isinstance(recapture, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no same-revision re-capture")
    recapture_directory = corpus_child(
        artifact_root,
        corpus_string(recapture, "directory", profile_name),
        f"same-revision re-capture directory for '{profile_name}'",
    )
    comparison_path = recapture_directory / "comparison.json"
    if sha256_file(comparison_path) != corpus_string(recapture, "comparison_sha256", profile_name):
        raise SmokeFailure(f"renderer baseline corpus re-capture checksum does not match for '{profile_name}'")
    try:
        comparison = json.loads(comparison_path.read_text(encoding="utf-8"))
    except OSError as error:
        raise SmokeFailure(f"could not read renderer baseline corpus re-capture '{comparison_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"renderer baseline corpus re-capture is not valid JSON: {comparison_path}: {error}") from error
    if not isinstance(comparison, dict) or comparison.get("schema") != "nwb.renderer-baseline-comparison.v1":
        raise SmokeFailure(f"renderer baseline corpus re-capture has an unsupported schema for '{profile_name}'")
    if comparison.get("profile") != profile_name:
        raise SmokeFailure(f"renderer baseline corpus re-capture profile does not match for '{profile_name}'")
    expected_revision = corpus_string(reference, "source_revision", profile_name)
    if comparison.get("reference_source_revision") != expected_revision:
        raise SmokeFailure(f"renderer baseline corpus re-capture reference revision does not match for '{profile_name}'")
    if comparison.get("candidate_source_revision") != expected_revision:
        raise SmokeFailure(f"renderer baseline corpus re-capture candidate revision does not match for '{profile_name}'")
    difference = comparison.get("difference")
    if not isinstance(difference, dict):
        raise SmokeFailure(f"renderer baseline corpus re-capture has no difference metrics for '{profile_name}'")
    for corpus_key, comparison_key, limit_key in (
        ("max_abs", "max_abs", "maximum_max_abs"),
        ("mean_abs", "mean_abs", "maximum_mean_abs"),
        ("changed_fraction", "changed_fraction", "maximum_changed_fraction"),
    ):
        recorded_value = corpus_number(recapture, corpus_key, profile_name)
        observed_value = corpus_number(difference, comparison_key, profile_name)
        if observed_value != recorded_value:
            raise SmokeFailure(f"renderer baseline corpus re-capture metric does not match for '{profile_name}': {corpus_key}")
        if recorded_value > limits[limit_key]:
            raise SmokeFailure(f"renderer baseline corpus limit is below its observed re-capture for '{profile_name}'")
    return manifest, capture, CorpusReference(corpus_id, reference_directory, entry, limits)


def apply_corpus_limits(args: argparse.Namespace, reference: CorpusReference) -> None:
    for argument_name, corpus_key in (
        ("maximum_max_abs", "maximum_max_abs"),
        ("maximum_mean_abs", "maximum_mean_abs"),
        ("maximum_changed_fraction", "maximum_changed_fraction"),
    ):
        approved_limit = reference.limits[corpus_key]
        requested_limit = getattr(args, argument_name)
        if requested_limit is None:
            setattr(args, argument_name, approved_limit)
        elif requested_limit > approved_limit:
            raise SmokeFailure(
                f"--{argument_name.replace('_', '-')}={requested_limit} relaxes the immutable corpus limit "
                f"{approved_limit}; omit it or choose a stricter value"
            )


def verify_corpus(corpus_id: str, supplied_root: Optional[Path]) -> int:
    corpus = load_corpus(corpus_id)
    profiles = corpus.get("profiles")
    assert isinstance(profiles, dict)
    expected_profiles = set(profile_names())
    actual_profiles = set(profiles)
    if actual_profiles != expected_profiles:
        missing = sorted(expected_profiles - actual_profiles)
        unexpected = sorted(actual_profiles - expected_profiles)
        raise SmokeFailure(
            f"renderer baseline corpus '{corpus_id}' profile set mismatch: missing={missing}, unexpected={unexpected}"
        )
    capture_environment = corpus.get("capture_environment")
    assert isinstance(capture_environment, dict)
    gpu_validation = capture_environment.get("gpu_validation")
    assert isinstance(gpu_validation, bool)
    for profile_name in sorted(expected_profiles):
        profile = get_profile(profile_name)
        load_corpus_reference(
            corpus_id,
            supplied_root,
            profile_name,
            profile,
            gpu_validation,
            canonical_frozen_environment(profile),
        )
    print(f"renderer baseline corpus verified: {corpus_id}")
    return 0


def prepare_output_directory(path: Path) -> None:
    if path.exists():
        raise SmokeFailure(f"baseline output directory already exists and will not be overwritten: {path}")
    try:
        path.mkdir(parents=True)
    except OSError as error:
        raise SmokeFailure(f"could not create baseline output directory '{path}': {error}") from error


def run(args: argparse.Namespace) -> int:
    profile = get_profile(args.profile)
    args.settle_seconds = profile.settle_seconds if args.settle_seconds is None else args.settle_seconds
    if args.settle_seconds <= 0.0:
        raise SmokeFailure("--settle-seconds must be positive")
    frozen_environment = effective_frozen_environment(profile)
    reference_manifest: Optional[Mapping[str, object]] = None
    reference_capture: Optional[Path] = None
    corpus_reference: Optional[CorpusReference] = None
    reference_directory: Optional[Path] = None
    if args.reference_corpus is not None:
        reference_manifest, reference_capture, corpus_reference = load_corpus_reference(
            args.reference_corpus,
            args.corpus_root,
            args.profile,
            profile,
            args.gpu_validation,
            frozen_environment,
        )
        reference_directory = corpus_reference.reference_directory
        apply_corpus_limits(args, corpus_reference)
    elif args.reference_dir is not None:
        reference_manifest, reference_capture = load_reference(
            args.reference_dir,
            args.profile,
            frozen_environment,
            args.settle_seconds,
            args.gpu_validation,
            profile.capture_freeze_frame,
            profile.fixed_delta_seconds,
        )
        reference_directory = args.reference_dir

    if reference_capture is None and not source_worktree_clean():
        raise SmokeFailure("refusing to create an immutable baseline from a dirty source worktree")

    prepare_output_directory(args.output_dir)
    capture_kind = "candidate" if reference_capture is not None else "baseline"
    capture_path = args.output_dir / ("candidate.bmp" if reference_capture is not None else "baseline.bmp")
    log_path = args.output_dir / "runtime.log"
    capture = capture_scene(args, profile, capture_path, log_path, frozen_environment)
    manifest = manifest_payload(capture_kind, args.profile, profile, args, capture, reference_manifest)
    write_json(args.output_dir / "manifest.json", manifest)

    if reference_capture is None:
        print(f"captured immutable renderer baseline: {args.output_dir}")
        return 0

    difference = compare_bmp_rgb(reference_capture, capture_path, args.output_dir / "difference.bmp")
    failures = difference_failures(args, difference)
    comparison = {
        "schema": "nwb.renderer-baseline-comparison.v1",
        "profile": args.profile,
        "reference_directory": str(reference_directory),
        "candidate_directory": str(args.output_dir),
        "reference_source_revision": reference_manifest.get("source_revision"),
        "candidate_source_revision": capture.source_revision,
        "reference_capture": str(reference_capture),
        "candidate_capture": str(capture_path),
        "difference_capture": str(args.output_dir / "difference.bmp"),
        "difference": asdict(difference) | {"changed_fraction": difference.changed_fraction},
        "reference_corpus": corpus_reference.corpus_id if corpus_reference is not None else None,
        "effective_limits": {
            "maximum_max_abs": args.maximum_max_abs,
            "maximum_mean_abs": args.maximum_mean_abs,
            "maximum_changed_fraction": args.maximum_changed_fraction,
            "require_exact": args.require_exact,
        },
        "threshold_failures": failures,
        "verdict": "fail" if failures else ("pass" if corpus_reference is not None else "report-only"),
    }
    write_json(args.output_dir / "comparison.json", comparison)
    print(f"renderer baseline comparison artifacts: {args.output_dir}")
    if failures:
        print("renderer baseline comparison failed: " + "; ".join(failures), file=sys.stderr)
        return 1
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=profile_names(), help="Pinned renderer scene to capture.")
    parser.add_argument("--executable", type=Path, help="Selected smoke executable.")
    parser.add_argument("--runtime-dir", type=Path, help="Selected cooked smoke runtime directory.")
    parser.add_argument("--output-dir", type=Path, help="New, empty artifact directory.")
    reference_group = parser.add_mutually_exclusive_group()
    reference_group.add_argument("--reference-dir", type=Path, help="Immutable baseline directory to compare against.")
    reference_group.add_argument(
        "--reference-corpus",
        choices=(CURRENT_CORPUS_ID,),
        help="Compare against the checked-in formal current-renderer corpus and enforce its limits.",
    )
    parser.add_argument(
        "--corpus-root",
        type=Path,
        help="Restored artifact root for --reference-corpus or --verify-corpus (defaults to the corpus artifact root).",
    )
    parser.add_argument(
        "--verify-corpus",
        choices=(CURRENT_CORPUS_ID,),
        help="Verify every restored current-renderer corpus artifact without running Vulkan.",
    )
    parser.add_argument("--logserver-executable", type=Path, help="Override the logserver executable.")
    parser.add_argument("--no-logserver", action="store_true", help="Use standalone loader logs instead of logserver.")
    validation_group = parser.add_mutually_exclusive_group()
    validation_group.add_argument("--gpu-validation", dest="gpu_validation", action="store_true")
    validation_group.add_argument("--no-gpu-validation", dest="gpu_validation", action="store_false")
    parser.set_defaults(gpu_validation=False)
    parser.add_argument("--settle-seconds", type=float, help="Override the profile's fixed temporal settle duration.")
    parser.add_argument("--startup-timeout", type=float, default=30.0, help="Seconds to wait for the native smoke window.")
    parser.add_argument("--require-exact", action="store_true", help="Require bit-exact RGB output for a comparison.")
    parser.add_argument("--maximum-max-abs", type=int, help="Optional maximum per-channel RGB difference.")
    parser.add_argument("--maximum-mean-abs", type=float, help="Optional maximum mean absolute RGB difference.")
    parser.add_argument("--maximum-changed-fraction", type=float, help="Optional maximum fraction of changed pixels.")
    parser.add_argument("--reject-log", action="append", default=[], help="Additional runtime log text that invalidates a capture.")
    parser.add_argument("--self-test", action="store_true", help="Exercise manifest and pixel-difference logic without Vulkan.")
    return parser


def validate_args(args: argparse.Namespace) -> None:
    if args.self_test:
        return
    if args.verify_corpus is not None:
        conflicting = [
            name
            for name, value in (
                ("--profile", args.profile),
                ("--executable", args.executable),
                ("--runtime-dir", args.runtime_dir),
                ("--output-dir", args.output_dir),
                ("--reference-dir", args.reference_dir),
                ("--reference-corpus", args.reference_corpus),
            )
            if value is not None
        ]
        if conflicting:
            raise SmokeFailure("--verify-corpus cannot be combined with " + ", ".join(conflicting))
        return
    if args.corpus_root is not None and args.reference_corpus is None:
        raise SmokeFailure("--corpus-root requires --reference-corpus or --verify-corpus")
    missing = [
        flag
        for flag, value in (("--profile", args.profile), ("--executable", args.executable), ("--runtime-dir", args.runtime_dir), ("--output-dir", args.output_dir))
        if value is None
    ]
    if missing:
        raise SmokeFailure("renderer baseline runner requires " + ", ".join(missing))
    if args.startup_timeout <= 0.0:
        raise SmokeFailure("--startup-timeout must be positive")
    if args.maximum_max_abs is not None and args.maximum_max_abs < 0:
        raise SmokeFailure("--maximum-max-abs must not be negative")
    if args.maximum_mean_abs is not None and args.maximum_mean_abs < 0.0:
        raise SmokeFailure("--maximum-mean-abs must not be negative")
    if args.maximum_changed_fraction is not None and not 0.0 <= args.maximum_changed_fraction <= 1.0:
        raise SmokeFailure("--maximum-changed-fraction must be in [0, 1]")


def run_self_test() -> int:
    assert FORBIDDEN_LOG_MESSAGES[:len(STRICT_LOG_FAILURE_MESSAGES)] == STRICT_LOG_FAILURE_MESSAGES
    with tempfile.TemporaryDirectory(prefix="nwb_renderer_baseline_") as temporary_directory:
        root = Path(temporary_directory)
        reference_directory = root / "opaque-texture" / "reference"
        reference_directory.mkdir(parents=True)
        reference_image = reference_directory / "baseline.bmp"
        candidate_image = root / "candidate.bmp"
        write_bmp_rgb(reference_image, 2, 1, bytes((8, 16, 24, 32, 40, 48)))
        write_bmp_rgb(candidate_image, 2, 1, bytes((8, 16, 24, 35, 40, 50)))
        manifest = {
            "schema": SCHEMA,
            "capture_kind": "baseline",
            "profile": "opaque-texture",
            "capture_file": "baseline.bmp",
            "capture_sha256": sha256_file(reference_image),
            "frozen_environment": {},
            "gpu_validation": False,
            "settle_seconds": 4.0,
            "capture_mode": "settled",
            "capture_freeze_frame": 0,
            "fixed_delta_seconds": 0.0,
            "source_revision": "test-reference",
        }
        write_json(reference_directory / "manifest.json", manifest)
        (reference_directory / "runtime.log").write_text("test runtime log\n", encoding="utf-8")
        loaded_manifest, loaded_capture = load_reference(reference_directory, "opaque-texture", {}, 4.0, False, 0, 0.0)
        assert loaded_manifest["source_revision"] == "test-reference"
        assert loaded_capture == reference_image
        recapture_directory = root / "opaque-texture" / "recapture"
        recapture_directory.mkdir(parents=True)
        comparison_path = recapture_directory / "comparison.json"
        write_json(
            comparison_path,
            {
                "schema": "nwb.renderer-baseline-comparison.v1",
                "profile": "opaque-texture",
                "reference_source_revision": "test-reference",
                "candidate_source_revision": "test-reference",
                "difference": {
                    "max_abs": 2,
                    "mean_abs": 0.5,
                    "changed_fraction": 0.25,
                },
            },
        )
        corpus_path = root / "corpus.json"
        corpus = {
            "schema": CORPUS_SCHEMA,
            "corpus_id": CURRENT_CORPUS_ID,
            "artifact_root": ".",
            "capture_environment": {"gpu_validation": False},
            "profiles": {
                "opaque-texture": {
                    "reference": {
                        "directory": "opaque-texture/reference",
                        "source_revision": "test-reference",
                        "capture_sha256": sha256_file(reference_image),
                        "manifest_sha256": sha256_file(reference_directory / "manifest.json"),
                        "runtime_log_sha256": sha256_file(reference_directory / "runtime.log"),
                    },
                    "same_revision_recapture": {
                        "directory": "opaque-texture/recapture",
                        "comparison_sha256": sha256_file(comparison_path),
                        "max_abs": 2,
                        "mean_abs": 0.5,
                        "changed_fraction": 0.25,
                    },
                    "limits": {
                        "maximum_max_abs": 3,
                        "maximum_mean_abs": 1.0,
                        "maximum_changed_fraction": 0.75,
                    },
                }
            },
        }
        write_json(corpus_path, corpus)
        corpus_manifest, corpus_capture, corpus_reference = load_corpus_reference(
            CURRENT_CORPUS_ID,
            root,
            "opaque-texture",
            get_profile("opaque-texture"),
            False,
            corpus_path=corpus_path,
        )
        assert corpus_manifest["source_revision"] == "test-reference"
        assert corpus_capture == reference_image
        assert corpus_reference.limits["maximum_max_abs"] == 3.0
        reference_log = reference_directory / "runtime.log"
        reference_log.write_text("tampered runtime log\n", encoding="utf-8")
        try:
            load_corpus_reference(
                CURRENT_CORPUS_ID,
                root,
                "opaque-texture",
                get_profile("opaque-texture"),
                False,
                corpus_path=corpus_path,
            )
        except SmokeFailure:
            pass
        else:
            raise AssertionError("corpus artifact tampering must fail closed")
        reference_log.write_text("test runtime log\n", encoding="utf-8")
        corpus_args = SimpleNamespace(
            maximum_max_abs=None,
            maximum_mean_abs=None,
            maximum_changed_fraction=None,
        )
        apply_corpus_limits(corpus_args, corpus_reference)
        assert corpus_args.maximum_mean_abs == 1.0
        corpus_args.maximum_max_abs = 4
        try:
            apply_corpus_limits(corpus_args, corpus_reference)
        except SmokeFailure:
            pass
        else:
            raise AssertionError("corpus comparison limits must not be relaxed")
        transparent_profile = get_profile("transparent-avboit")
        assert transparent_profile.capture_freeze_frame == 96
        assert transparent_profile.settle_seconds == 0.75
        assert transparent_profile.fixed_delta_seconds == 1.0 / 60.0
        skinned_csg_profile = get_profile("skinned-csg")
        assert skinned_csg_profile.capture_freeze_frame == 96
        assert skinned_csg_profile.settle_seconds == 0.75
        assert skinned_csg_profile.fixed_delta_seconds == 1.0 / 60.0
        caustic_profile = get_profile("caustics")
        assert caustic_profile.capture_freeze_frame == 360
        assert caustic_profile.settle_seconds == 0.75
        assert caustic_profile.fixed_delta_seconds == 1.0 / 60.0
        surfel_profile = get_profile("surfel-gi")
        assert surfel_profile.capture_freeze_frame == 360
        assert surfel_profile.settle_seconds == 0.75
        assert surfel_profile.fixed_delta_seconds == 1.0 / 60.0
        soft_shadow_profile = get_profile("soft-shadows")
        assert soft_shadow_profile.capture_freeze_frame == 360
        assert soft_shadow_profile.settle_seconds == 0.75
        assert soft_shadow_profile.fixed_delta_seconds == 1.0 / 60.0
        stress_profile = get_profile("stress")
        assert stress_profile.capture_freeze_frame == 96
        assert stress_profile.settle_seconds == 0.75
        assert stress_profile.fixed_delta_seconds == 1.0 / 60.0
        difference = compare_bmp_rgb(reference_image, candidate_image, root / "difference.bmp")
        assert difference.width == 2 and difference.height == 1
        assert difference.max_abs == 3
        assert difference.changed_pixels == 1
        args = SimpleNamespace(require_exact=False, maximum_max_abs=2, maximum_mean_abs=None, maximum_changed_fraction=None)
        assert difference_failures(args, difference) == ["max abs 3 exceeds 2"]
        args.require_exact = True
        assert difference_failures(args, difference)
    print("renderer-baseline runner self-test passed")
    return 0


def main(argv: Sequence[str]) -> int:
    args = make_parser().parse_args(argv)
    try:
        validate_args(args)
        if args.self_test:
            return run_self_test()
        if args.verify_corpus is not None:
            return verify_corpus(args.verify_corpus, args.corpus_root)
        return run(args)
    except SmokeSkip as error:
        print(f"renderer baseline skipped: {error}", file=sys.stderr)
        return SKIP_EXIT_CODE
    except SmokeFailure as error:
        print(f"renderer baseline failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
