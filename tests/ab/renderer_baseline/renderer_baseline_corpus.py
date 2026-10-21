"""Renderer-baseline reference corpus: frozen-environment identity, reference manifests, and corpus loading and verification."""


from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

REPO = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO / "tests" / "smoke"))

from profiles import BaselineProfile, corpus_profile_names, get_profile, profile_names  # noqa: E402
from window_capture_smoke import (  # noqa: E402
    FRAMEBUFFER_CAPTURE_SKIP_MESSAGE, SKIP_EXIT_CODE, STRICT_LOG_FAILURE_MESSAGES, SmokeFailure, SmokeSkip, WindowsCapture, read_bmp_24, wait_for_application_capture_exit,
    build_launch_environment, make_runtime_launch_args, create_capture_backend, ensure_process_running, launch_logserver, launch_testbed, require_normal_process_exit,
    shutdown_logserver_and_collect, terminate_process, validate_capture_result, wait_for_log_message,)


LIT_FRAME_LOCKED = "frame-locked"
LIT_SETTLED = "settled"
LIT_UTF_8 = "utf-8"
LIT_SCHEMA = "schema"
LIT_CAPTURE_KIND = "capture_kind"
LIT_PROFILE = "profile"
LIT_CAPTURE_MODE = "capture_mode"
LIT_CAPTURE_FREEZE_FRAME = "capture_freeze_frame"
LIT_FIXED_DELTA_SECONDS = "fixed_delta_seconds"
LIT_SETTLE_SECONDS = "settle_seconds"
LIT_GPU_VALIDATION = "gpu_validation"
LIT_SOURCE_REVISION = "source_revision"
LIT_CAPTURE_FILE = "capture_file"
LIT_CAPTURE_SHA256 = "capture_sha256"
LIT_FROZEN_ENVIRONMENT = "frozen_environment"
LIT_REFERENCE_SOURCE_REVISION = "reference_source_revision"
LIT_MANIFEST_JSON = "manifest.json"
LIT_BASELINE = "baseline"
LIT_CORPUS_ID = "corpus_id"
LIT_ARTIFACT_ROOT = "artifact_root"
LIT_PROFILES = "profiles"
LIT_CAPTURE_ENVIRONMENT = "capture_environment"
LIT_REFERENCE = "reference"
LIT_DIRECTORY = "directory"
LIT_RUNTIME_LOG = "runtime.log"
LIT_MANIFEST_SHA256 = "manifest_sha256"
LIT_RUNTIME_LOG_SHA256 = "runtime_log_sha256"
LIT_LIMITS = "limits"
LIT_MAXIMUM_MAX_ABS = "maximum_max_abs"
LIT_MAXIMUM_MEAN_ABS = "maximum_mean_abs"
LIT_MAXIMUM_CHANGED_FRACTION = "maximum_changed_fraction"
LIT_SAME_REVISION_RECAPTURE = "same_revision_recapture"
LIT_COMPARISON_JSON = "comparison.json"
LIT_COMPARISON_SHA256 = "comparison_sha256"
LIT_NWB_RENDERER_BASELINE_COMPARISON_V1 = "nwb.renderer-baseline-comparison.v1"
LIT_CANDIDATE_SOURCE_REVISION = "candidate_source_revision"
LIT_DIFFERENCE = "difference"
LIT_MAX_ABS = "max_abs"
LIT_MEAN_ABS = "mean_abs"
LIT_CHANGED_FRACTION = "changed_fraction"
SCHEMA = "nwb.renderer-baseline.v1"
CORPUS_SCHEMA = "nwb.renderer-baseline-corpus.v1"
CURRENT_CORPUS_FILE = Path(__file__).with_name("current_renderer_corpus.json")


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


def effective_frozen_environment(profile: BaselineProfile) -> Dict[str, str]:
    return { name: os.environ.get(name, default_value) for name, default_value in sorted(profile.frozen_environment.items()) }


def canonical_frozen_environment(profile: BaselineProfile) -> Dict[str, str]:
    return dict(sorted(profile.frozen_environment.items()))


def load_reference(reference_directory: Path, profile_name: str, frozen_environment: Mapping[str, str], settle_seconds: float, gpu_validation: bool, capture_freeze_frame: int, fixed_delta_seconds: float,) -> Tuple[Mapping[str, object], Path]:
    manifest_path = reference_directory / LIT_MANIFEST_JSON
    try:
        manifest = json.loads(manifest_path.read_text(encoding=LIT_UTF_8))
    except OSError as error:
        raise SmokeFailure(f"could not read baseline manifest '{manifest_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"baseline manifest is not valid JSON: {manifest_path}: {error}") from error
    if not isinstance(manifest, dict) or manifest.get(LIT_SCHEMA) != SCHEMA:
        raise SmokeFailure(f"baseline manifest has an unsupported schema: {manifest_path}")
    if manifest.get(LIT_CAPTURE_KIND) != LIT_BASELINE:
        raise SmokeFailure(f"reference manifest is not an immutable baseline: {manifest_path}")
    if manifest.get(LIT_PROFILE) != profile_name:
        raise SmokeFailure(f"baseline profile mismatch: reference is {manifest.get(LIT_PROFILE)!r}, candidate is {profile_name!r}")
    if manifest.get(LIT_FROZEN_ENVIRONMENT) != dict(frozen_environment):
        raise SmokeFailure("baseline frozen environment differs from the candidate capture")
    if manifest.get(LIT_GPU_VALIDATION) != gpu_validation:
        raise SmokeFailure("baseline GPU-validation mode differs from the candidate capture")
    if manifest.get(LIT_SETTLE_SECONDS) != settle_seconds:
        raise SmokeFailure("baseline settle duration differs from the candidate capture")
    expected_capture_mode = LIT_FRAME_LOCKED if capture_freeze_frame != 0 else LIT_SETTLED
    if manifest.get(LIT_CAPTURE_MODE) != expected_capture_mode:
        raise SmokeFailure("baseline capture mode differs from the candidate capture")
    if manifest.get(LIT_CAPTURE_FREEZE_FRAME) != capture_freeze_frame:
        raise SmokeFailure("baseline capture freeze frame differs from the candidate capture")
    if manifest.get(LIT_FIXED_DELTA_SECONDS) != fixed_delta_seconds:
        raise SmokeFailure("baseline fixed simulation delta differs from the candidate capture")
    capture_name = manifest.get(LIT_CAPTURE_FILE)
    if not isinstance(capture_name, str) or not capture_name:
        raise SmokeFailure(f"baseline manifest has no capture filename: {manifest_path}")
    capture_path = reference_directory / capture_name
    if not capture_path.is_file():
        raise SmokeFailure(f"baseline capture does not exist: {capture_path}")
    expected_hash = manifest.get(LIT_CAPTURE_SHA256)
    if not isinstance(expected_hash, str) or sha256_file(capture_path) != expected_hash:
        raise SmokeFailure(f"baseline capture checksum does not match its immutable manifest: {capture_path}")
    return manifest, capture_path


def load_corpus(corpus_id: str, corpus_path: Path = CURRENT_CORPUS_FILE) -> Mapping[str, object]:
    try:
        corpus = json.loads(corpus_path.read_text(encoding=LIT_UTF_8))
    except OSError as error:
        raise SmokeFailure(f"could not read renderer baseline corpus '{corpus_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"renderer baseline corpus is not valid JSON: {corpus_path}: {error}") from error
    if not isinstance(corpus, dict) or corpus.get(LIT_SCHEMA) != CORPUS_SCHEMA:
        raise SmokeFailure(f"renderer baseline corpus has an unsupported schema: {corpus_path}")
    if corpus.get(LIT_CORPUS_ID) != corpus_id:
        raise SmokeFailure(f"renderer baseline corpus id does not match '{corpus_id}': {corpus_path}")
    if not isinstance(corpus.get(LIT_ARTIFACT_ROOT), str):
        raise SmokeFailure(f"renderer baseline corpus has no artifact root: {corpus_path}")
    if not isinstance(corpus.get(LIT_PROFILES), dict):
        raise SmokeFailure(f"renderer baseline corpus has no profile table: {corpus_path}")
    return corpus


def corpus_artifact_root(corpus: Mapping[str, object], supplied_root: Optional[Path]) -> Path:
    if supplied_root is not None:
        return supplied_root
    artifact_root = corpus.get(LIT_ARTIFACT_ROOT)
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
    try:
        number = float(value)
    except OverflowError as error:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no finite {key}") from error
    if not math.isfinite(number):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no finite {key}")
    return number


def load_corpus_reference(corpus_id: str, supplied_root: Optional[Path], profile_name: str, profile: BaselineProfile, gpu_validation: bool, frozen_environment: Optional[Mapping[str, str]] = None, corpus_path: Path = CURRENT_CORPUS_FILE,
) -> Tuple[Mapping[str, object], Path, CorpusReference]:
    corpus = load_corpus(corpus_id, corpus_path)
    capture_environment = corpus.get(LIT_CAPTURE_ENVIRONMENT)
    if not isinstance(capture_environment, dict):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no capture environment")
    expected_gpu_validation = capture_environment.get(LIT_GPU_VALIDATION)
    if not isinstance(expected_gpu_validation, bool):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no GPU-validation mode")
    if gpu_validation != expected_gpu_validation:
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' requires gpu_validation={expected_gpu_validation}, " f"got {gpu_validation}")

    profiles = corpus.get(LIT_PROFILES)
    assert isinstance(profiles, dict)
    entry = profiles.get(profile_name)
    if not isinstance(entry, dict):
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' has no profile '{profile_name}'")
    reference = entry.get(LIT_REFERENCE)
    if not isinstance(reference, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no reference")
    artifact_root = corpus_artifact_root(corpus, supplied_root)
    reference_directory = corpus_child(artifact_root, corpus_string(reference, LIT_DIRECTORY, profile_name), f"reference directory for '{profile_name}'",)
    manifest_path = reference_directory / LIT_MANIFEST_JSON
    log_path = reference_directory / LIT_RUNTIME_LOG
    if sha256_file(manifest_path) != corpus_string(reference, LIT_MANIFEST_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus manifest checksum does not match for '{profile_name}'")
    if sha256_file(log_path) != corpus_string(reference, LIT_RUNTIME_LOG_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus runtime-log checksum does not match for '{profile_name}'")

    manifest, capture = load_reference(reference_directory, profile_name, effective_frozen_environment(profile) if frozen_environment is None else frozen_environment, profile.settle_seconds, gpu_validation, profile.capture_freeze_frame,
        profile.fixed_delta_seconds,)
    if manifest.get(LIT_SOURCE_REVISION) != corpus_string(reference, LIT_SOURCE_REVISION, profile_name):
        raise SmokeFailure(f"renderer baseline corpus source revision does not match for '{profile_name}'")
    if manifest.get(LIT_CAPTURE_SHA256) != corpus_string(reference, LIT_CAPTURE_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus capture manifest does not match for '{profile_name}'")
    if sha256_file(capture) != corpus_string(reference, LIT_CAPTURE_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus image checksum does not match for '{profile_name}'")

    limits_entry = entry.get(LIT_LIMITS)
    if not isinstance(limits_entry, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no comparison limits")
    limits = { LIT_MAXIMUM_MAX_ABS: corpus_number(limits_entry, LIT_MAXIMUM_MAX_ABS, profile_name), LIT_MAXIMUM_MEAN_ABS: corpus_number(limits_entry, LIT_MAXIMUM_MEAN_ABS, profile_name),
        LIT_MAXIMUM_CHANGED_FRACTION: corpus_number(limits_entry, LIT_MAXIMUM_CHANGED_FRACTION, profile_name), }
    if limits[LIT_MAXIMUM_MAX_ABS] < 0.0 or limits[LIT_MAXIMUM_MEAN_ABS] < 0.0:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has negative comparison limits")
    if not 0.0 <= limits[LIT_MAXIMUM_CHANGED_FRACTION] <= 1.0:
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has an invalid changed-fraction limit")
    recapture = entry.get(LIT_SAME_REVISION_RECAPTURE)
    if not isinstance(recapture, dict):
        raise SmokeFailure(f"renderer baseline corpus entry '{profile_name}' has no same-revision re-capture")
    recapture_directory = corpus_child(artifact_root, corpus_string(recapture, LIT_DIRECTORY, profile_name), f"same-revision re-capture directory for '{profile_name}'",)
    comparison_path = recapture_directory / LIT_COMPARISON_JSON
    if sha256_file(comparison_path) != corpus_string(recapture, LIT_COMPARISON_SHA256, profile_name):
        raise SmokeFailure(f"renderer baseline corpus re-capture checksum does not match for '{profile_name}'")
    try:
        comparison = json.loads(comparison_path.read_text(encoding=LIT_UTF_8))
    except OSError as error:
        raise SmokeFailure(f"could not read renderer baseline corpus re-capture '{comparison_path}': {error}") from error
    except json.JSONDecodeError as error:
        raise SmokeFailure(f"renderer baseline corpus re-capture is not valid JSON: {comparison_path}: {error}") from error
    if not isinstance(comparison, dict) or comparison.get(LIT_SCHEMA) != LIT_NWB_RENDERER_BASELINE_COMPARISON_V1:
        raise SmokeFailure(f"renderer baseline corpus re-capture has an unsupported schema for '{profile_name}'")
    if comparison.get(LIT_PROFILE) != profile_name:
        raise SmokeFailure(f"renderer baseline corpus re-capture profile does not match for '{profile_name}'")
    expected_revision = corpus_string(reference, LIT_SOURCE_REVISION, profile_name)
    if comparison.get(LIT_REFERENCE_SOURCE_REVISION) != expected_revision:
        raise SmokeFailure(f"renderer baseline corpus re-capture reference revision does not match for '{profile_name}'")
    if comparison.get(LIT_CANDIDATE_SOURCE_REVISION) != expected_revision:
        raise SmokeFailure(f"renderer baseline corpus re-capture candidate revision does not match for '{profile_name}'")
    difference = comparison.get(LIT_DIFFERENCE)
    if not isinstance(difference, dict):
        raise SmokeFailure(f"renderer baseline corpus re-capture has no difference metrics for '{profile_name}'")
    for corpus_key, comparison_key, limit_key in ((LIT_MAX_ABS, LIT_MAX_ABS, LIT_MAXIMUM_MAX_ABS), (LIT_MEAN_ABS, LIT_MEAN_ABS, LIT_MAXIMUM_MEAN_ABS), (LIT_CHANGED_FRACTION, LIT_CHANGED_FRACTION, LIT_MAXIMUM_CHANGED_FRACTION),):
        recorded_value = corpus_number(recapture, corpus_key, profile_name)
        observed_value = corpus_number(difference, comparison_key, profile_name)
        if observed_value != recorded_value:
            raise SmokeFailure(f"renderer baseline corpus re-capture metric does not match for '{profile_name}': {corpus_key}")
        if recorded_value > limits[limit_key]:
            raise SmokeFailure(f"renderer baseline corpus limit is below its observed re-capture for '{profile_name}'")
    return manifest, capture, CorpusReference(corpus_id, reference_directory, entry, limits)


def apply_corpus_limits(args: argparse.Namespace, reference: CorpusReference) -> None:
    for argument_name, corpus_key in ((LIT_MAXIMUM_MAX_ABS, LIT_MAXIMUM_MAX_ABS), (LIT_MAXIMUM_MEAN_ABS, LIT_MAXIMUM_MEAN_ABS), (LIT_MAXIMUM_CHANGED_FRACTION, LIT_MAXIMUM_CHANGED_FRACTION),):
        approved_limit = reference.limits[corpus_key]
        requested_limit = getattr(args, argument_name)
        if requested_limit is None:
            setattr(args, argument_name, approved_limit)
        elif requested_limit > approved_limit:
            raise SmokeFailure(f"--{argument_name.replace('_', '-')}={requested_limit} relaxes the immutable corpus limit " f"{approved_limit}; omit it or choose a stricter value")


def verify_corpus(corpus_id: str, supplied_root: Optional[Path]) -> int:
    corpus = load_corpus(corpus_id)
    profiles = corpus.get(LIT_PROFILES)
    assert isinstance(profiles, dict)
    expected_profiles = set(corpus_profile_names())
    actual_profiles = set(profiles)
    if actual_profiles != expected_profiles:
        missing = sorted(expected_profiles - actual_profiles)
        unexpected = sorted(actual_profiles - expected_profiles)
        raise SmokeFailure(f"renderer baseline corpus '{corpus_id}' profile set mismatch: missing={missing}, unexpected={unexpected}")
    capture_environment = corpus.get(LIT_CAPTURE_ENVIRONMENT)
    assert isinstance(capture_environment, dict)
    gpu_validation = capture_environment.get(LIT_GPU_VALIDATION)
    assert isinstance(gpu_validation, bool)
    for profile_name in sorted(expected_profiles):
        profile = get_profile(profile_name)
        load_corpus_reference(corpus_id, supplied_root, profile_name, profile, gpu_validation, canonical_frozen_environment(profile),)
    print(f"renderer baseline corpus verified: {corpus_id}")
    return 0
