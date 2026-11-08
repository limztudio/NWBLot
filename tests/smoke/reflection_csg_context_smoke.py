#!/usr/bin/env python3
"""Live small/dense/ordinary CSG reflection transitions with exact accepted source-frame evidence."""


import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

from reflection_csg_context_evidence import LIT_CONTEXT_CAPTURES_COMPLETE, ReflectionCsgContextEvidence
from reflection_csg_context_images import LIT_CONTEXT_TRANSITION_BMP, ReflectionCsgContextImageEvidence
from reflection_smoke import DEFAULT_RAY_BUDGET, capture_environment
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90


LIT_EXECUTABLE = "--executable"
LIT_WORKING_DIRECTORY = "--working-directory"
LIT_LOGSERVER_EXECUTABLE = "--logserver-executable"
LIT_TIMEOUT = "--timeout"
LIT_UTF_8 = "utf-8"
LIT_METRICS = "metrics"

CASE = "optical_csg_context_transition"




def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(LIT_EXECUTABLE, type=Path, required=True)
    parser.add_argument(LIT_WORKING_DIRECTORY, type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument(LIT_LOGSERVER_EXECUTABLE, type=Path)
    parser.add_argument(LIT_TIMEOUT, type=float, default=180)
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    directory = args.output_directory.resolve()
    if directory.exists() and any(directory.iterdir()):
        parser.error("output directory must be empty; completed evidence is never overwritten")
    directory.mkdir(parents=True, exist_ok=True)
    output, log = directory / LIT_CONTEXT_TRANSITION_BMP, directory / "runtime.log"
    command = [sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")), LIT_EXECUTABLE, str(args.executable), LIT_WORKING_DIRECTORY, str(args.working_directory),
        "--output", str(output), "--application-capture", "--application-capture-frame-count", "1", LIT_TIMEOUT, str(args.timeout), "--log-output", str(log)]
    for message in ("ReflectionSmokeProject: case " + CASE + " created", "ReflectionSmokeProject: hardware available", "ReflectionSmokeProject: reflection mode hardware", "Reflection resolve: hardware",
        LIT_CONTEXT_CAPTURES_COMPLETE, "ReflectionSmokeProject: shutdown"):
        command += ["--expect-log-message", message]
    if args.logserver_executable:
        command += [LIT_LOGSERVER_EXECUTABLE, str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    environment = capture_environment(CASE, "hardware", DEFAULT_RAY_BUDGET)
    environment.update({"NWB_REFLECTION_SMOKE_OPTICAL_QUERIES": "16", "NWB_REFLECTION_SMOKE_TEMPORAL": "0", "NWB_REFLECTION_SMOKE_SPATIAL": "0", "NWB_REFLECTION_SMOKE_HISTORY_SAMPLES": "16", "NWB_REFLECTION_SMOKE_DIAGNOSTICS": "1"})
    try:
        result = subprocess.run(command, env=environment, check=False, timeout=args.timeout + LIT_CAPTURE_TIMEOUT_MARGIN_SECONDS)
        if result.returncode == SKIP_EXIT_CODE:
            return SKIP_EXIT_CODE
        if result.returncode:
            raise SmokeFailure("live CSG context capture failed with exit " + str(result.returncode))
        evidence = ReflectionCsgContextEvidence.analyze_evidence(log.read_text(encoding=LIT_UTF_8))
        manifest = {"frame_source": "actual application framebuffer readback", "size": [960, 720], "environment_overrides": {name: environment[name] for name in environment if name.startswith("NWB_")}, "completed_phase_evidence": evidence, LIT_METRICS: None,
            "diagnostic_sampling": "The owning probe samples each fixture frame after public graphics waitForIdle, retaining exact-source latest-only statistics.",
            "limitations": "Five live context transitions qualify HW reflection semantics and accepted packet coverage with synchronized frames; no asynchronous-overlap, performance or GI claim."}
        report = directory / "manifest.json"
        report.write_text(json.dumps(manifest, indent=2) + "\n", encoding=LIT_UTF_8)
        manifest[LIT_METRICS] = ReflectionCsgContextImageEvidence.analyze_images(directory, evidence)
        report.write_text(json.dumps(manifest, indent=2) + "\n", encoding=LIT_UTF_8)
        print("PASS: live small/dense/ordinary CSG contexts, exact accepted source frames, packet coverage and image invariance", flush=True)
        return 0
    except (SmokeFailure, OSError, subprocess.TimeoutExpired) as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
