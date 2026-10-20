"""Publish accepted UI capture checkpoints and retain failed-stage evidence."""
from __future__ import annotations

import time

from window_capture_smoke import SmokeFailure

LIT_STAGE = "stage"
LIT_CAPTURE = "capture"
LIT_NATIVE_WINDOW = "native_window"

# Shared displayed-state gate timing for every UI fixture driver.
CHECKPOINT_SETTLE_SECONDS = 0.25
CHECKPOINT_STAGE_TIMEOUT_SECONDS = 10.0
CHECKPOINT_POLL_SECONDS = 0.1


def prepare_guarded_checkpoint(run, name, changes, dynamic, settle):
    run.expected.update(changes)
    for field in dynamic:
        run.expected.pop(field, None)
    run.native.maintain_pointer()
    time.sleep(settle)
    path = run.args.output_directory / f"{len(run.stages):02d}_{name}.bmp"
    return path, min(run.deadline, time.monotonic() + CHECKPOINT_STAGE_TIMEOUT_SECONDS)


def accept_checkpoint(run, name, path, snapshot, report, dynamic):
    run.snapshot = snapshot
    for field in dynamic:
        run.expected[field] = snapshot[field]
    report.update({LIT_STAGE: name, LIT_CAPTURE: str(path), LIT_NATIVE_WINDOW: run.native.observe_window()})
    run.stages.append(report)
    run.write_report(False)


def fail_checkpoint(run, name, path, report, fixture):
    run.failure_report = {LIT_STAGE: name, LIT_CAPTURE: str(path), LIT_NATIVE_WINDOW: run.native.observe_window(), "observation": report}
    run.write_report(False)
    raise SmokeFailure(f"{fixture} displayed-state gate '{name}' failed: {run.failure_report}")
