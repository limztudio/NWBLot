"""Freeze and verify renderer benchmark artifact identities."""

import json
from pathlib import Path
import re

from smoke_volume_identity import file_identity, runtime_pipeline_cache_paths
from window_capture_smoke import SmokeFailure


LIT_SHA256 = "sha256"
LIT_SOURCE = "source"
LIT_UTF_8 = "utf-8"
LIT_FILES = "files"
LIT_REVISION = "revision"
LIT_RES = "res"
LIT_CRASH_HANDLER_EXE = "crash_handler.exe"
LIT_EXECUTABLE = "executable"
LIT_RUNTIME = "runtime"
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


class RendererBenchmarkIdentity:
    @staticmethod
    def source_identity(manifest_path):
        document = json.loads(manifest_path.read_text(encoding=LIT_UTF_8))
        if not isinstance(document, dict):
            raise SmokeFailure("source manifest must be an object")
        files = document.get(LIT_FILES)
        if not isinstance(document.get(LIT_REVISION), str) or not document[LIT_REVISION].strip() \
            or not isinstance(files, dict) or not files:
            raise SmokeFailure("source manifest requires a revision and nonempty files-to-SHA256 mapping")
        actual = {}
        for relative, expected in files.items():
            if not isinstance(relative, str) or not isinstance(expected, str) or not SHA256.fullmatch(expected):
                raise SmokeFailure("source manifest contains an invalid path or SHA256")
            path = (manifest_path.parent / relative).resolve()
            if not path.is_file() or file_identity(path)[LIT_SHA256] != expected:
                raise SmokeFailure(f"source bytes differ from the manifest: {relative}")
            actual[str(path)] = expected
        return {"manifest": str(manifest_path), "identity": file_identity(manifest_path), LIT_REVISION: document[LIT_REVISION], LIT_FILES: actual}

    @staticmethod
    def runtime_identity(runtime):
        resources = runtime / LIT_RES
        if not resources.is_dir():
            raise SmokeFailure(f"runtime lacks a packed res directory: {runtime}")
        mutable = set(runtime_pipeline_cache_paths(runtime))
        files = {path.relative_to(runtime).as_posix(): file_identity(path) for path in sorted(resources.rglob("*")) if path.is_file() and path not in mutable}
        volumes = {relative: identity[LIT_SHA256] for relative, identity in files.items() if Path(relative).match("*.vol")}
        if not files or not volumes:
            raise SmokeFailure("runtime contains no authored packed volume")
        return {LIT_FILES: files, "authored_volume_hashes": volumes}

    @staticmethod
    def binary_identity(executable):
        # Freeze local loader dependencies and the crash helper, not unrelated sibling test executables or logger output created at shutdown.
        directory = executable.parent
        dependencies = {path for path in directory.iterdir() if path.is_file() and (path.suffix.lower() in (".dll", ".dylib") or ".so" in path.suffixes or path.name in ("crash_handler", LIT_CRASH_HANDLER_EXE))}
        if executable.suffix.lower() == ".exe" and not (directory / LIT_CRASH_HANDLER_EXE).is_file():
            raise SmokeFailure("Windows smoke launcher requires its sibling crash_handler.exe")
        dependencies.add(executable)
        return {path.name: file_identity(path) for path in sorted(dependencies)}

    @staticmethod
    def freeze_arm(arm):
        return {LIT_EXECUTABLE: str(arm.executable), LIT_RUNTIME: str(arm.runtime), "binaries": RendererBenchmarkIdentity.binary_identity(arm.executable), "resources": RendererBenchmarkIdentity.runtime_identity(arm.runtime), LIT_SOURCE: RendererBenchmarkIdentity.source_identity(arm.source_manifest)}

    @staticmethod
    def verify_frozen(arms, identities, shared):
        RendererBenchmarkIdentity.validate_arm_separation(arms)
        for arm in arms:
            if RendererBenchmarkIdentity.freeze_arm(arm) != identities[arm.name]:
                raise SmokeFailure(f"frozen source, executable, dependencies, or authored runtime changed: {arm.name}")
        for path, identity in shared.items():
            if file_identity(Path(path)) != identity:
                raise SmokeFailure(f"shared runner, symbols, or logserver changed: {path}")

    @staticmethod
    def cache_identity(runtime):
        return {path.relative_to(runtime).as_posix(): file_identity(path) for path in runtime_pipeline_cache_paths(runtime)}

    @staticmethod
    def validate_arm_separation(arms):
        first, second = arms
        if first.runtime == second.runtime or first.runtime.is_relative_to(second.runtime) \
            or second.runtime.is_relative_to(first.runtime) or (first.runtime / LIT_RES).resolve() == (second.runtime / LIT_RES).resolve():
            raise SmokeFailure("arms need distinct non-nested runtime/resource directories and separate mutable caches")
        second_caches = runtime_pipeline_cache_paths(second.runtime)
        for first_cache in runtime_pipeline_cache_paths(first.runtime):
            for second_cache in second_caches:
                if first_cache.samefile(second_cache):
                    raise SmokeFailure("arm runtime caches alias the same physical file")

