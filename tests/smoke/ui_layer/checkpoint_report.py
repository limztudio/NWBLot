"""Publish accepted UI capture checkpoints and retain failed-stage evidence."""
from __future__ import annotations

import time

from window_capture_smoke import SmokeFailure


def prepare_guarded_checkpoint(run, name, changes, dynamic, settle):
    run.expected.update(changes)
    for field in dynamic:
        run.expected.pop(field, None)
    run.native.maintain_pointer()
    time.sleep(settle)
    path = run.args.output_directory / f"{len(run.stages):02d}_{name}.bmp"
    return path, min(run.deadline, time.monotonic() + 10.0)


def accept_checkpoint(run, name, path, snapshot, report, dynamic):
    run.snapshot = snapshot
    for field in dynamic:
        run.expected[field] = snapshot[field]
    report.update({"stage": name, "capture": str(path), "native_window": run.native.observe_window()})
    run.stages.append(report)
    run.write_report(False)


def fail_checkpoint(run, name, path, report, fixture):
    run.failure_report = {"stage": name, "capture": str(path), "native_window": run.native.observe_window(), "observation": report}
    run.write_report(False)
    raise SmokeFailure(f"{fixture} displayed-state gate '{name}' failed: {run.failure_report}")
