"""Run matched public UI CPU workloads from independently built executables."""

import argparse
import hashlib
import json
import math
import os
import platform
import statistics
import subprocess
import time
from pathlib import Path

LIT_NAME = "name"
LIT_MEDIANNSPEROPERATION = "medianNsPerOperation"
LIT_ITERATIONSPERSAMPLE = "iterationsPerSample"
LIT_BASELINE = "baseline"
LIT_CANDIDATE = "candidate"
LIT_PATH = "PATH"
LIT_UTF_8 = "utf-8"
LIT_SETUPCHECKSUM = "setupChecksum"
LIT_BEHAVIORCHECKSUM = "behaviorChecksum"
LIT_VARIANT = "variant"
LIT_CASES = "cases"


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def existing_file(value):
    path = Path(value).resolve(strict=True)
    if not path.is_file():
        raise argparse.ArgumentTypeError(f"Not a file: {path}")
    return path


def existing_directory(value):
    path = Path(value).resolve(strict=True)
    if not path.is_dir():
        raise argparse.ArgumentTypeError(f"Not a directory: {path}")
    return path


def parse_cases(output):
    cases = {}
    for line in output.splitlines():
        if not line.startswith("{"):
            continue
        case = json.loads(line)
        if case.get("type") != "ui_input_benchmark":
            continue
        name = case[LIT_NAME]
        if name in cases:
            raise RuntimeError(f"Duplicate benchmark case: {name}")
        samples = case["samplesNsPerOperation"]
        if len(samples) != 11 or any(not math.isfinite(sample) or sample <= 0 for sample in samples):
            raise RuntimeError(f"Invalid timing samples: {name}")
        if not math.isclose(statistics.median(samples), case[LIT_MEDIANNSPEROPERATION], rel_tol=1e-12):
            raise RuntimeError(f"Incorrect sample median: {name}")
        if not isinstance(case[LIT_ITERATIONSPERSAMPLE], int) or case[LIT_ITERATIONSPERSAMPLE] <= 0:
            raise RuntimeError(f"Invalid batch size: {name}")
        cases[name] = case
    if not cases:
        raise RuntimeError("Executable produced no public UI benchmark cases")
    return cases


def source_metadata(source_hash, manifest):
    if not source_hash.strip():
        raise ValueError("Source hash metadata must be nonempty")
    metadata = {"sourceHash": source_hash}
    if manifest:
        metadata["manifestPath"] = str(manifest)
        metadata["manifestSha256"] = file_hash(manifest)
        metadata["manifest"] = json.loads(manifest.read_text(encoding="utf-8-sig"))
    return metadata


def measure(options):
    options.output.mkdir(parents=True, exist_ok=True)
    executables = {LIT_BASELINE: options.baseline, LIT_CANDIDATE: options.candidate}
    hashes = {variant: file_hash(path) for variant, path in executables.items()}
    runtime_hashes = { str(path): file_hash(path) for directory in options.runtime_directory for path in sorted(directory.glob("*.dll")) }
    metadata = { LIT_BASELINE: source_metadata(options.baseline_source_hash, options.baseline_source_manifest), LIT_CANDIDATE: source_metadata(options.candidate_source_hash, options.candidate_source_manifest), }
    reference = None
    trials = []
    for pair in range(options.pairs):
        order = (LIT_BASELINE, LIT_CANDIDATE) if pair % 2 == 0 else (LIT_CANDIDATE, LIT_BASELINE)
        for variant in order:
            executable = executables[variant]
            if file_hash(executable) != hashes[variant]:
                raise RuntimeError(f"Executable changed during measurement: {executable}")
            environment = os.environ.copy()
            paths = [executable.parent, *options.runtime_directory]
            environment[LIT_PATH] = os.pathsep.join(map(str, paths)) + os.pathsep + environment.get(LIT_PATH, "")
            started = time.monotonic()
            result = subprocess.run([str(executable)], cwd=options.working_directory, env=environment, capture_output=True, text=True, encoding=LIT_UTF_8, errors="replace", timeout=options.timeout,)
            prefix = options.output / f"pair_{pair:02}_{variant}"
            prefix.with_suffix(".stdout.log").write_text(result.stdout, encoding=LIT_UTF_8)
            prefix.with_suffix(".stderr.log").write_text(result.stderr, encoding=LIT_UTF_8)
            if result.returncode:
                raise RuntimeError(f"{variant} exited {result.returncode}; see {prefix}.stdout.log and {prefix}.stderr.log")
            cases = parse_cases(result.stdout)
            checks = { name: {LIT_SETUPCHECKSUM: case[LIT_SETUPCHECKSUM], LIT_BEHAVIORCHECKSUM: case[LIT_BEHAVIORCHECKSUM]} for name, case in cases.items() }
            if reference is None:
                reference = checks
            elif checks != reference:
                raise RuntimeError(f"Workload geometry or behavior checksum changed in pair {pair}: {variant}")
            trials.append({"pair": pair, LIT_VARIANT: variant, "elapsedSeconds": time.monotonic() - started, LIT_CASES: cases})
            print(f"Pair {pair + 1}/{options.pairs}: {variant} completed {len(cases)} matched cases", flush=True)
    for variant, executable in executables.items():
        if file_hash(executable) != hashes[variant]:
            raise RuntimeError(f"Executable changed during measurement: {executable}")
    for filename, expected in runtime_hashes.items():
        if file_hash(Path(filename)) != expected:
            raise RuntimeError(f"Runtime library changed during measurement: {filename}")
    summary = []
    for name in sorted(reference):
        values = { variant: [trial[LIT_CASES][name][LIT_MEDIANNSPEROPERATION] for trial in trials if trial[LIT_VARIANT] == variant] for variant in executables }
        baseline = statistics.median(values[LIT_BASELINE])
        candidate = statistics.median(values[LIT_CANDIDATE])
        summary.append({ LIT_NAME: name, "baselineMedianNsPerOperation": baseline, "candidateMedianNsPerOperation": candidate, "reductionPercent": 100.0 * (1.0 - candidate / baseline),
            "baselineProcessMedians": values[LIT_BASELINE], "candidateProcessMedians": values[LIT_CANDIDATE], **reference[name], })
    report = { "host": {"platform": platform.platform(), "machine": platform.machine(), "python": platform.python_version()}, "pairs": options.pairs, "order": "Alternating AB and BA process pairs", "workingDirectory": str(options.working_directory),
        "executables": {variant: {"path": str(path), "sha256": hashes[variant], **metadata[variant]} for variant, path in executables.items()}, "runtimeDllSha256": runtime_hashes, "summary": summary, "trials": trials, }
    output = options.output / "report.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding=LIT_UTF_8)
    print(f"Matched measurement report: {output}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=existing_file)
    parser.add_argument("--candidate", required=True, type=existing_file)
    parser.add_argument("--baseline-source-hash", required=True)
    parser.add_argument("--candidate-source-hash", required=True)
    parser.add_argument("--baseline-source-manifest", type=existing_file)
    parser.add_argument("--candidate-source-manifest", type=existing_file)
    parser.add_argument("--runtime-directory", action="append", default=[], type=existing_directory)
    parser.add_argument("--working-directory", default=Path.cwd(), type=existing_directory)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--pairs", default=4, type=int)
    parser.add_argument("--timeout", default=180.0, type=float)
    options = parser.parse_args()
    options.output = options.output.resolve()
    if options.pairs < 4 or options.pairs > 64 or options.timeout <= 0:
        parser.error("Use 4 to 64 process pairs and a positive per-process timeout")
    measure(options)


if __name__ == "__main__":
    main()
