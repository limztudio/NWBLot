from __future__ import annotations

import time

from checkpoint_report import CHECKPOINT_SETTLE_SECONDS
from fixture_launch import UiFixtureRunBase, run_ui_fixture
from guarded_native import GuardedNativeInput

LIT_IMAGE = "image"


class PassiveImageRun(UiFixtureRunBase):
    def __init__(self, args, backend, handle, process, log_directory, log_baseline, log_pattern, *, minimum_height, snapshot_reader, observer, center, fixture_label=LIT_IMAGE):
        super().__init__(args, backend, handle, process, log_directory, log_baseline, log_pattern, native=GuardedNativeInput(backend, handle))
        self.minimum_height = minimum_height
        self.snapshot_reader = snapshot_reader
        self.observer = observer
        self.fixture_label = fixture_label

    def click(self, name):
        super().click(name, error_label=f"{self.fixture_label} fixture has no displayed placement for")

    def capture_checkpoint(self, name, *, extent, extra, stage_timeout, changes):
        self.expected.update(changes)
        self.native.pointer(4, 4)
        self.native.maintain_pointer()
        time.sleep(CHECKPOINT_SETTLE_SECONDS)
        self.poll_checkpoint(name, stage_timeout=stage_timeout, minimum_extent=(740.0, self.minimum_height),
            minimum_extent_message=f"the {self.fixture_label} fixture needs a logical client of at least 740x{self.minimum_height:g}",
            process_stage=f"during {self.fixture_label} UI capture", snapshot_reader=self.snapshot_reader,
            evaluate=lambda frame, snapshot: self.observer(frame, snapshot, self.expected, extent=extent, skin=self.args.skin, extra=extra),
            raw_windows=True, failure_label=self.fixture_label, maintain_pointer=True, accept_extras=lambda _: {"native_window": self.native.observe_window()}, failure_extras=True)


def run_passive_image(args, *, fixture, display_marker, driver_type, log_name, label, fixture_label=LIT_IMAGE):
    return run_ui_fixture(args, fixture=fixture, display_marker=display_marker, driver_type=driver_type, log_name=log_name, label=label,
        window_label=fixture_label, process_label=fixture_label, raw_windows=True, restore_pointer=True)
